// Cloud drive upload: the steps follow the fixed api-enhanced module/cloud.js and
// plugins/songUpload.js (MIT, see NOTICE) and the official app 9.6.22; native implementation,
// HTTPS only (docs/api/cloud-drive.md).
#include "cloud_upload.h"
#include "logging/log.h"
#include "music_mapping.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <utility>

namespace cirrus::netease
{
using namespace domain;

namespace
{
// The audio bucket's upload host (the NOS load balancer names it; it serves HTTPS too).
constexpr auto kNosHost = "https://nosup-jd1.127.net";
constexpr auto kBucket = "jd-musicrep-privatecloud-audio-public";
// What the reference sends when it doesn't know (module/cloud.js).
constexpr qint64 kUnknownBitrate = 999000;

struct Allocation {
    QString objectKey;
    QByteArray token;
    QString resourceId;
};

std::optional<Allocation> allocationFrom(const QJsonObject& root)
{
    static const QRegularExpression key(
        QStringLiteral("\\A[A-Za-z0-9_.=-]+(/[A-Za-z0-9_.=-]+)*\\z"));
    const auto value = root.value(QStringLiteral("result"));
    if (!value.isObject())
        return std::nullopt;
    const auto o = value.toObject();
    const auto objectKey = o.value(QStringLiteral("objectKey"));
    const auto token = o.value(QStringLiteral("token"));
    if (!objectKey.isString() || objectKey.toString().size() > 512 ||
        !key.match(objectKey.toString()).hasMatch() || !token.isString())
        return std::nullopt;
    for (const auto& part : objectKey.toString().split(u'/'))
        if (part == u"." || part == u"..")
            return std::nullopt;
    const auto bytes = token.toString().toLatin1();
    if (bytes.isEmpty() || bytes.size() > 4096 || QString::fromLatin1(bytes) != token.toString())
        return std::nullopt;
    for (unsigned char c : bytes)
        if (c <= 0x20 || c >= 0x7f)
            return std::nullopt;
    const auto resource = mapping::providerId(o.value(QStringLiteral("resourceId")));
    if (resource.isEmpty())
        return std::nullopt;
    return Allocation{objectKey.toString(), bytes, resource};
}

// What a reply's field is, without its value: for cloud.invalid.
QString fieldKind(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::String: {
        const QString s = v.toString();
        const bool digits =
            !s.isEmpty() && std::all_of(s.cbegin(), s.cend(), [](QChar c) { return c.isDigit(); });
        return QStringLiteral("string:%1%2").arg(s.size()).arg(digits ? ":digits" : "");
    }
    case QJsonValue::Double: {
        const double d = v.toDouble();
        return d == 0                             ? QStringLiteral("number:zero")
               : std::abs(d) > 9007199254740991.0 ? QStringLiteral("number:unsafe")
               : d != std::floor(d)               ? QStringLiteral("number:fraction")
               : d < 0                            ? QStringLiteral("number:negative")
                                                  : QStringLiteral("number");
    }
    case QJsonValue::Bool:
        return QStringLiteral("bool");
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Object:
        return QStringLiteral("object");
    case QJsonValue::Array:
        return QStringLiteral("array");
    default:
        return QStringLiteral("missing");
    }
}

QByteArray contentType(const QString& extension)
{
    static const QHash<QString, QByteArray> types{
        {QStringLiteral("mp3"), "audio/mpeg"}, {QStringLiteral("flac"), "audio/flac"},
        {QStringLiteral("m4a"), "audio/mp4"},  {QStringLiteral("aac"), "audio/aac"},
        {QStringLiteral("ogg"), "audio/ogg"},  {QStringLiteral("opus"), "audio/ogg"},
        {QStringLiteral("wav"), "audio/wav"},  {QStringLiteral("wma"), "audio/x-ms-wma"},
        {QStringLiteral("ape"), "audio/x-ape"}};
    return types.value(extension, "application/octet-stream");
}

// A file's text as the cloud song's details: trimmed, controls dropped, bounded.
QString detailText(QString text, qsizetype maximum)
{
    text.removeIf([](QChar c) { return c.unicode() < 0x20 || c.unicode() == 0x7f; });
    text = text.trimmed();
    if (text.size() > maximum)
        text.truncate(maximum);
    return text;
}
} // namespace

std::optional<CloudFileFacts> readCloudFile(const QString& path, const std::atomic_bool& canceled)
{
    static const QRegularExpression extension(QStringLiteral("\\A[a-z0-9]{1,8}\\z"));
    if (!CloudUploadFile{path}.isValid())
        return std::nullopt;
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable() || info.size() <= 0)
        return std::nullopt;
    CloudFileFacts facts;
    facts.fileName = info.fileName();
    facts.extension = info.suffix().toLower();
    if (!extension.match(facts.extension).hasMatch() || facts.fileName.size() > 255)
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QCryptographicHash md5(QCryptographicHash::Md5);
    QByteArray block(1024 * 1024, Qt::Uninitialized);
    qint64 size = 0;
    for (;;) {
        if (canceled.load())
            return std::nullopt;
        const auto n = file.read(block.data(), block.size());
        if (n < 0)
            return std::nullopt;
        if (n == 0)
            break;
        md5.addData(QByteArrayView(block.constData(), n));
        size += n;
    }
    facts.size = size;
    facts.md5 = QString::fromLatin1(md5.result().toHex());
    return size > 0 ? std::optional(facts) : std::nullopt;
}

QString cloudAllocationName(const QString& fileName)
{
    QString base = QFileInfo(fileName).completeBaseName();
    base.removeIf([](QChar c) { return c.isSpace() || c.unicode() < 0x20 || c.unicode() == 0x7f; });
    base.replace(u'.', u'_');
    if (base.size() > 200)
        base.truncate(200);
    return base.isEmpty() ? QStringLiteral("audio") : base;
}

struct CloudUploadGateway::Pending {
    SessionToken context;
    QString accountId;
    QString path;
    CloudUploadStep step = CloudUploadStep::Reading;
    CloudFileFacts facts;
    QString checkedSong; // the check's songId, what the details are submitted for
    bool needUpload = true;
    Allocation allocation;
    QString song; // the submitted song's id
    quint64 apiId = 0;
    QPointer<QNetworkReply> reply;
    QFile* file = nullptr;
    QByteArray body;
    std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
    bool anySent = false;
    bool transferred = false;
    QElapsedTimer stepTimer, total;
    qint64 lastProgressMs = -1;
};

CloudUploadGateway::CloudUploadGateway(application::SessionCoordinator& session,
                                       SessionApiClient& client, CloudTagReader tags,
                                       std::unique_ptr<QNetworkAccessManager> nos, QObject* parent)
    : QObject(parent), m_session(session), m_client(&client), m_tags(std::move(tags))
{
    if (!nos)
        nos = std::make_unique<QNetworkAccessManager>();
    Q_ASSERT(!nos->parent() && nos->thread() == thread());
    nos->setParent(this);
    m_nos = nos.release();
    Q_ASSERT(session.thread() == thread() && client.thread() == thread());
    connect(&client, &SessionApiClient::completed, this,
            [this](quint64 apiId, SessionToken, const ApiResult& api) {
                const auto id = m_apiIds.take(apiId);
                if (id && m_pending.contains(id)) {
                    m_pending[id]->apiId = 0;
                    apiDone(id, api);
                }
            });
    connect(&session, &application::SessionCoordinator::changed, this, [this](SessionSnapshot) {
        for (const auto id : m_pending.keys())
            if (const auto p = m_pending.value(id); p && !current(*p))
                fail(id, CloudUploadError::StaleContext);
    });
}

CloudUploadGateway::~CloudUploadGateway()
{
    for (const auto& p : std::as_const(m_pending)) {
        p->canceled->store(true);
        if (p->reply) {
            p->reply->disconnect(this);
            p->reply->abort();
        }
        if (m_client && p->apiId)
            m_client->cancel(p->apiId);
    }
}

bool CloudUploadGateway::current(const Pending& p) const
{
    const auto s = m_session.snapshot();
    return m_client && m_session.accepts(p.context) && s.phase == SessionPhase::Authenticated &&
           s.accountId == p.accountId;
}

quint64 CloudUploadGateway::submit(SessionToken context, QString accountId, CloudUploadFile file)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= MaximumPending)
        return 0;
    const auto id = ++m_nextId;
    auto p = std::make_shared<Pending>();
    p->context = context;
    p->accountId = std::move(accountId);
    p->path = std::move(file.path);
    p->total.start();
    m_pending.insert(id, p);
    auto error = CloudUploadError::None;
    if (!m_session.accepts(context))
        error = CloudUploadError::StaleContext;
    else if (m_session.snapshot().phase != SessionPhase::Authenticated)
        error = CloudUploadError::AuthenticationRequired;
    else if (!validMusicId(p->accountId) || p->accountId != m_session.snapshot().accountId)
        error = CloudUploadError::StaleContext;
    else if (!CloudUploadFile{p->path}.isValid())
        error = CloudUploadError::InvalidFile;
    QTimer::singleShot(0, this, [this, id, error] {
        if (error != CloudUploadError::None)
            fail(id, error);
        else
            read(id);
    });
    return id;
}

void CloudUploadGateway::enter(quint64 id, CloudUploadStep step)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    if (step != CloudUploadStep::Reading)
        log::debug(lcApi, "cloud.upload_step", {{"step", p->step}, {"ms", p->stepTimer.elapsed()}});
    p->step = step;
    p->stepTimer.start();
    emit progressed(id, {step, 0, p->facts.size});
}

void CloudUploadGateway::read(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    enter(id, CloudUploadStep::Reading);
    QPointer<CloudUploadGateway> self(this);
    QThreadPool::globalInstance()->start(
        [self, id, path = p->path, canceled = p->canceled, tags = m_tags] {
            auto facts = readCloudFile(path, *canceled);
            if (facts && tags && !canceled->load())
                tags(path, *facts);
            if (self)
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, id, facts] {
                        if (self)
                            self->readDone(id, facts);
                    },
                    Qt::QueuedConnection);
        });
}

void CloudUploadGateway::readDone(quint64 id, std::optional<CloudFileFacts> facts)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    if (p->canceled->load()) {
        fail(id, CloudUploadError::Canceled);
        return;
    }
    if (!current(*p)) {
        fail(id, CloudUploadError::StaleContext);
        return;
    }
    if (!facts) {
        fail(id, CloudUploadError::InvalidFile);
        return;
    }
    if (facts->size > CloudUploadFile::MaximumBytes) {
        fail(id, CloudUploadError::TooLarge);
        return;
    }
    p->facts = std::move(*facts);
    const qint64 bitrate =
        p->facts.bitrate && *p->facts.bitrate > 0 ? *p->facts.bitrate : kUnknownBitrate;
    request(id, CloudUploadStep::Checking, ApiOperation::CloudUploadCheck,
            {{QStringLiteral("md5"), p->facts.md5},
             {QStringLiteral("length"), QString::number(p->facts.size)},
             {QStringLiteral("bitrate"), QString::number(bitrate)}});
}

void CloudUploadGateway::request(quint64 id, CloudUploadStep step, ApiOperation operation,
                                 QJsonObject parameters)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    enter(id, step);
    p->apiId = m_client ? m_client->submit(p->context, operation, std::move(parameters)) : 0;
    if (!p->apiId) {
        fail(id, CloudUploadError::Busy);
        return;
    }
    m_apiIds.insert(p->apiId, id);
}

void CloudUploadGateway::apiDone(quint64 id, const ApiResult& api)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    p->anySent = p->anySent || api.submittedToTransport;
    if (p->canceled->load()) {
        fail(id, CloudUploadError::Canceled);
        return;
    }
    if (!current(*p)) {
        fail(id, CloudUploadError::StaleContext);
        return;
    }
    if (api.httpStatus == 429 || api.decoded.businessCode == 429) {
        fail(id, CloudUploadError::RateLimited, 429);
        return;
    }
    if (api.error != ApiError::None || !api.decoded.ok() || !api.decoded.businessCode) {
        auto error = CloudUploadError::InvalidResponse;
        if (api.error == ApiError::Canceled)
            error = CloudUploadError::Canceled;
        else if (api.error == ApiError::StaleContext)
            error = CloudUploadError::StaleContext;
        else if (api.error == ApiError::Busy)
            error = CloudUploadError::Busy;
        else if (api.error == ApiError::Cookie)
            error = CloudUploadError::AuthenticationRequired;
        else if (api.error == ApiError::InvalidRequest)
            error = CloudUploadError::InvalidFile;
        else if (api.transportError == TransportError::Timeout)
            error = CloudUploadError::Timeout;
        else if (api.error == ApiError::Transport)
            error = CloudUploadError::Network;
        fail(id, error);
        return;
    }
    const int code = *api.decoded.businessCode;
    if (code != 200) {
        fail(id, CloudUploadError::ProviderRejected, code);
        return;
    }
    const auto& root = api.decoded.object;
    switch (p->step) {
    case CloudUploadStep::Checking: {
        const auto need = root.value(QStringLiteral("needUpload"));
        p->checkedSong = mapping::providerId(root.value(QStringLiteral("songId")));
        if (!need.isBool() || p->checkedSong.isEmpty()) {
            invalid(id, need.isBool() ? "songId" : "needUpload", root);
            return;
        }
        p->needUpload = need.toBool();
        request(id, CloudUploadStep::Allocating, ApiOperation::CloudUploadAllocate,
                {{QStringLiteral("md5"), p->facts.md5},
                 {QStringLiteral("ext"), p->facts.extension},
                 {QStringLiteral("filename"), cloudAllocationName(p->facts.fileName)}});
        return;
    }
    case CloudUploadStep::Allocating: {
        const auto allocation = allocationFrom(root);
        if (!allocation) {
            invalid(id, "result", root);
            return;
        }
        p->allocation = *allocation;
        if (p->needUpload)
            transfer(id);
        else
            transferDone(id); // already on the server: straight to the details
        return;
    }
    case CloudUploadStep::Submitting: {
        p->song = mapping::providerId(root.value(QStringLiteral("songId")));
        if (p->song.isEmpty()) {
            invalid(id, "songId", root);
            return;
        }
        request(id, CloudUploadStep::Publishing, ApiOperation::CloudPublish,
                {{QStringLiteral("songid"), p->song}});
        return;
    }
    case CloudUploadStep::Publishing: {
        CloudUploadResult result;
        result.outcome = CloudUploadOutcome::Uploaded;
        result.step = CloudUploadStep::Publishing;
        result.song = CloudSongId{p->song};
        result.transferred = p->transferred;
        finish(id, result);
        return;
    }
    default:
        fail(id, CloudUploadError::InvalidResponse);
    }
}

void CloudUploadGateway::transfer(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    enter(id, CloudUploadStep::Transferring);
    auto* file = new QFile(p->path, this);
    if (!file->open(QIODevice::ReadOnly) || file->size() != p->facts.size) {
        delete file;
        fail(id, CloudUploadError::InvalidFile); // changed since it was read
        return;
    }
    p->file = file;
    const QByteArray encodedKey = QUrl::toPercentEncoding(p->allocation.objectKey);
    const QUrl url = QUrl::fromEncoded(QByteArray(kNosHost) + '/' + kBucket + '/' + encodedKey +
                                           "?offset=0&complete=true&version=1.0",
                                       QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != u"https") {
        fail(id, CloudUploadError::InvalidResponse);
        return;
    }
    QNetworkRequest wire(url);
    wire.setHeader(QNetworkRequest::ContentTypeHeader, contentType(p->facts.extension));
    wire.setHeader(QNetworkRequest::ContentLengthHeader, p->facts.size);
    wire.setRawHeader("x-nos-token", p->allocation.token);
    wire.setRawHeader("Content-MD5", p->facts.md5.toLatin1());
    wire.setRawHeader("Accept-Encoding", "identity");
    wire.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::ManualRedirectPolicy);
    wire.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    wire.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    wire.setTransferTimeout(60000); // a stall, not the whole transfer
    auto* reply = m_nos->post(wire, file);
    p->reply = reply;
    p->anySent = true;
    p->allocation.token.clear();
    reply->setReadBufferSize(16 * 1024);
    connect(reply, &QNetworkReply::uploadProgress, this, [this, id](qint64 sent, qint64) {
        const auto p = m_pending.value(id);
        if (!p)
            return;
        // At most a few times a second: the UI draws a bar, not every block.
        const qint64 now = p->stepTimer.elapsed();
        if (sent < p->facts.size && p->lastProgressMs >= 0 && now - p->lastProgressMs < 250)
            return;
        p->lastProgressMs = now;
        emit progressed(id, {CloudUploadStep::Transferring, sent, p->facts.size});
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, id] {
        const auto p = m_pending.value(id);
        if (!p || !p->reply)
            return;
        p->body += p->reply->read(16 * 1024 + 1 - p->body.size());
        if (p->body.size() > 16 * 1024)
            fail(id, CloudUploadError::InvalidResponse);
    });
    connect(reply, &QNetworkReply::finished, this, [this, id] {
        const auto p = m_pending.value(id);
        if (!p || !p->reply)
            return;
        p->body += p->reply->read(16 * 1024 + 1 - p->body.size());
        const int status = p->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = p->reply->error();
        p->reply->disconnect(this);
        p->reply->deleteLater();
        p->reply = nullptr;
        if (p->file) {
            p->file->deleteLater();
            p->file = nullptr;
        }
        if (p->canceled->load()) {
            fail(id, CloudUploadError::Canceled);
            return;
        }
        if (error == QNetworkReply::TimeoutError ||
            error == QNetworkReply::OperationCanceledError) {
            fail(id, CloudUploadError::Timeout);
            return;
        }
        if (status == 429) {
            fail(id, CloudUploadError::RateLimited, 429);
            return;
        }
        if (error != QNetworkReply::NoError || status != 200) {
            log::warn(lcApi, "cloud.transfer_failed", {{"status", status}, {"error", int(error)}});
            fail(id,
                 status >= 400 && status < 500 ? CloudUploadError::ProviderRejected
                                               : CloudUploadError::Network,
                 status ? std::optional(status) : std::nullopt);
            return;
        }
        p->transferred = true;
        transferDone(id);
    });
}

void CloudUploadGateway::transferDone(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    if (!current(*p)) {
        fail(id, CloudUploadError::StaleContext);
        return;
    }
    // The file's tags as the song's details, the file name when it has none (as the reference).
    const auto name = detailText(QFileInfo(p->facts.fileName).completeBaseName(), 256);
    auto title = detailText(p->facts.title, 256);
    if (title.isEmpty())
        title = name.isEmpty() ? QStringLiteral("audio") : name;
    auto album = detailText(p->facts.album, 256);
    auto artist = detailText(p->facts.artist, 256);
    const qint64 bitrate =
        p->facts.bitrate && *p->facts.bitrate > 0 ? *p->facts.bitrate : kUnknownBitrate;
    request(id, CloudUploadStep::Submitting, ApiOperation::CloudUploadInfo,
            {{QStringLiteral("md5"), p->facts.md5},
             {QStringLiteral("songid"), p->checkedSong},
             {QStringLiteral("filename"), detailText(p->facts.fileName, 255)},
             {QStringLiteral("song"), title},
             {QStringLiteral("album"), album.isEmpty() ? QStringLiteral("未知专辑") : album},
             {QStringLiteral("artist"), artist.isEmpty() ? QStringLiteral("未知艺术家") : artist},
             {QStringLiteral("bitrate"), QString::number(bitrate)},
             {QStringLiteral("resourceId"), p->allocation.resourceId}});
}

void CloudUploadGateway::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto p = m_pending.value(id);
    if (!p)
        return;
    p->canceled->store(true);
    if (p->reply) {
        p->reply->abort(); // finished() reports it
        return;
    }
    if (p->apiId && m_client) {
        m_client->cancel(p->apiId); // completed() reports it
        return;
    }
    if (p->step != CloudUploadStep::Reading) // reading notices the flag itself
        QTimer::singleShot(0, this, [this, id] { fail(id, CloudUploadError::Canceled); });
}

void CloudUploadGateway::invalid(quint64 id, const char* field, const QJsonObject& root)
{
    // The reply's shape, never its values (docs/api/cloud-drive.md §3).
    if (const auto p = m_pending.value(id))
        log::warn(lcApi, "cloud.invalid",
                  {{"step", p->step},
                   {"field", field},
                   {"kind", fieldKind(root.value(QLatin1StringView(field)))},
                   {"keys", root.keys().join(u',')}});
    fail(id, CloudUploadError::InvalidResponse);
}

void CloudUploadGateway::fail(quint64 id, CloudUploadError error, std::optional<int> code)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    CloudUploadResult result;
    result.error = error;
    result.step = p->step;
    result.providerCode = code;
    result.transferred = p->transferred;
    // Nothing is in the cloud until the details are submitted; after that it may be.
    if (!p->anySent)
        result.outcome = CloudUploadOutcome::NotSent;
    else if (p->step >= CloudUploadStep::Submitting && error != CloudUploadError::ProviderRejected)
        result.outcome = CloudUploadOutcome::Unknown;
    else
        result.outcome = CloudUploadOutcome::Failed;
    finish(id, result);
}

void CloudUploadGateway::finish(quint64 id, CloudUploadResult result)
{
    const auto p = m_pending.take(id);
    if (!p)
        return;
    p->canceled->store(true);
    if (p->apiId) {
        m_apiIds.remove(p->apiId);
        if (m_client)
            m_client->cancel(p->apiId);
    }
    if (p->reply) {
        p->reply->disconnect(this);
        p->reply->abort();
        p->reply->deleteLater();
    }
    if (p->file)
        p->file->deleteLater();
    const bool fine = result.outcome == CloudUploadOutcome::Uploaded ||
                      result.error == CloudUploadError::Canceled;
    const log::Fields fields{
        {"outcome", result.outcome},
        {"error", result.error},
        {"step", result.step},
        {"transferred", result.transferred},
        {"size_kb", p->facts.size / 1024},
        {"ms", p->total.elapsed()},
        log::fieldIf(result.providerCode.has_value(), "code", result.providerCode.value_or(0))};
    log::write(fine ? QtInfoMsg : QtWarningMsg, lcApi,
               fine ? log::Event("cloud.upload_done") : log::Event("cloud.upload_failed"), fields);
    emit completed(id, p->context, result);
}
} // namespace cirrus::netease
