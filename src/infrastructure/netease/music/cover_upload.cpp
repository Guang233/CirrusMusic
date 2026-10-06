// Fixed api-enhanced plugins/upload.js (MIT); bounded native replacement, see NOTICE.
#include "cover_upload.h"
#include "domain/music/playlist_cover_types.h"
#include "music_mapping.h"
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <cstring>
#include <utility>
namespace cirrus::netease
{
namespace
{
bool safeAllocation(const CoverAllocation& a)
{
    static const QRegularExpression key(QStringLiteral("\\A[A-Za-z0-9_./-]{1,512}\\z"));
    if (!key.match(a.objectKey).hasMatch() || !domain::validMusicId(a.imageId) ||
        a.token.isEmpty() || a.token.size() > 4096)
        return false;
    for (const auto& part : a.objectKey.split(u'/'))
        if (part.isEmpty() || part == u"." || part == u"..")
            return false;
    for (unsigned char c : a.token)
        if (c <= 0x20 || c >= 0x7f)
            return false;
    return true;
}
} // namespace
std::optional<CoverAllocation> coverAllocation(const ApiResult& result)
{
    if (result.error != ApiError::None || !result.decoded.ok() ||
        result.decoded.businessCode != 200)
        return std::nullopt;
    const auto value = result.decoded.object.value(QStringLiteral("result"));
    if (!value.isObject())
        return std::nullopt;
    const auto o = value.toObject();
    const auto key = o.value(QStringLiteral("objectKey")), token = o.value(QStringLiteral("token"));
    if (!key.isString() || !token.isString())
        return std::nullopt;
    const auto text = token.toString();
    const auto bytes = text.toLatin1();
    if (QString::fromLatin1(bytes) != text)
        return std::nullopt;
    CoverAllocation a{key.toString(), bytes, mapping::providerId(o.value(QStringLiteral("docId")))};
    return safeAllocation(a) ? std::optional<CoverAllocation>(std::move(a)) : std::nullopt;
}
// Bounded structural preflight, NOT a replacement for Qt's full pixel decoding.
// Refuse unsupported JPEG forms and malformed marker boundaries before invoking the codec.
static bool jpegMarkers(const QByteArray& data)
{
    qsizetype pos = 2;
    bool entropy = false, frame = false, scan = false;
    int markers = 0;
    const auto byte = [&](qsizetype i) { return static_cast<unsigned char>(data[i]); };
    while (pos < data.size() && markers < 4096) {
        if (entropy) {
            while (pos < data.size() && byte(pos) != 0xff)
                ++pos;
        }
        if (pos >= data.size() || byte(pos++) != 0xff)
            return false;
        while (pos < data.size() && byte(pos) == 0xff)
            ++pos;
        if (pos >= data.size())
            return false;
        const auto marker = byte(pos++);
        if (entropy && (marker == 0 || (marker >= 0xd0 && marker <= 0xd7)))
            continue;
        entropy = false;
        ++markers;
        if (marker == 0xd9)
            return frame && scan && pos == data.size();
        const bool sof = marker == 0xc0 || marker == 0xc2;
        if (!(sof || marker == 0xc4 || marker == 0xdb || marker == 0xdd || marker == 0xda ||
              (marker >= 0xe0 && marker <= 0xef) || marker == 0xfe))
            return false;
        if (pos + 2 > data.size())
            return false;
        const int size = (byte(pos) << 8) | byte(pos + 1);
        if (size < 2 || pos + size > data.size())
            return false;
        if (sof) {
            if (frame || size < 11 || byte(pos + 2) != 8)
                return false;
            frame = true;
        }
        if (marker == 0xda) {
            if (!frame || size < 8)
                return false;
            scan = true;
            entropy = true;
        }
        pos += size;
    }
    return false;
}
QByteArray prepareCoverJpeg(const QByteArray& input)
{
    if (!(domain::PlaylistCoverReplace{{"1"}, input}).isValid() || !jpegMarkers(input))
        return {};
    QBuffer source;
    source.setData(input);
    source.open(QIODevice::ReadOnly);
    QImageReader reader(&source, "jpeg");
    reader.setAutoTransform(true);
    const auto size = reader.size();
    // Local budgets, no arbitrary crop/resize or provider image-size claim.
    if (!size.isValid() || size.width() != size.height() || size.width() > 1024)
        return {};
    auto decoded = reader.read();
    if (decoded.isNull() || decoded.size() != size)
        return {};
    decoded = decoded.convertToFormat(QImage::Format_RGB32);
    QImage clean(size, QImage::Format_RGB32); // fresh pixels: no text/EXIF/ICC/source filename
    if (clean.isNull() || decoded.isNull())
        return {};
    for (int y = 0; y < size.height(); ++y)
        std::memcpy(clean.scanLine(y), decoded.constScanLine(y), size.width() * 4);
    QByteArray encoded;
    QBuffer output(&encoded);
    output.open(QIODevice::WriteOnly);
    if (!clean.save(&output, "JPEG", 85) ||
        encoded.size() > domain::PlaylistCoverReplace::MaximumBytes)
        return {};
    return encoded;
}
struct CoverUpload::Pending {
    CoverAllocation allocation;
    QByteArray jpeg, body;
    qsizetype expectedSize = 0;
    QPointer<QNetworkReply> reply;
    bool dispatched = false;
    bool canceled = false;
};
CoverUpload::CoverUpload(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent)
    : QObject(parent)
{
    if (!manager)
        manager = std::make_unique<QNetworkAccessManager>();
    Q_ASSERT(!manager->parent() && manager->thread() == thread());
    manager->setParent(this);
    m_manager = manager.release();
}
CoverUpload::~CoverUpload()
{
    for (const auto& p : std::as_const(m_pending))
        if (p->reply) {
            p->reply->disconnect(this);
            p->reply->abort();
        }
}
quint64 CoverUpload::post(CoverAllocation allocation, QByteArray jpeg)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 8)
        return 0;
    auto p = std::make_shared<Pending>();
    p->allocation = std::move(allocation);
    p->jpeg = std::move(jpeg);
    p->expectedSize = p->jpeg.size();
    const auto id = ++m_nextId;
    m_pending.insert(id, p);
    QTimer::singleShot(0, this, [this, id] { start(id); });
    return id;
}
void CoverUpload::start(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    if (p->canceled) {
        finish(id, ApiError::Canceled);
        return;
    }
    if (!safeAllocation(p->allocation) ||
        !(domain::PlaylistCoverReplace{{"1"}, p->jpeg}).isValid()) {
        finish(id, ApiError::InvalidRequest);
        return;
    }
    QUrl url(QStringLiteral("https://nosup-hz1.127.net"));
    url.setPath(QStringLiteral("/yyimgs/") + p->allocation.objectKey);
    url.setQuery(QStringLiteral("offset=0&complete=true&version=1.0"));
    QNetworkRequest wire(url);
    wire.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
    wire.setRawHeader("x-nos-token", p->allocation.token);
    wire.setRawHeader("Accept-Encoding", "identity");
    wire.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::ManualRedirectPolicy);
    wire.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    wire.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    wire.setTransferTimeout(15000);
    auto* reply = m_manager->post(wire, p->jpeg);
    p->reply = reply;
    p->dispatched = true;
    p->jpeg.clear();
    p->allocation.token.clear();
    reply->setReadBufferSize(16 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, [this, id] { consume(id); });
    connect(reply, &QNetworkReply::finished, this, [this, id] {
        consume(id);
        if (m_pending.contains(id))
            finish(id);
    });
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this,
            [this, id] { finish(id, ApiError::Transport, TransportError::Timeout); });
    timer->start(20000);
}
void CoverUpload::consume(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p || !p->reply)
        return;
    p->body += p->reply->read(16 * 1024 + 1 - p->body.size());
    if (p->body.size() > 16 * 1024)
        finish(id, ApiError::Transport, TransportError::ResponseTooLarge);
}
void CoverUpload::cancel(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    p->canceled = true;
    QTimer::singleShot(0, this, [this, id] { finish(id, ApiError::Canceled); });
}
void CoverUpload::finish(quint64 id, ApiError error, TransportError transport)
{
    const auto p = m_pending.take(id);
    if (!p)
        return;
    if (p->canceled)
        error = ApiError::Canceled;
    ApiResult r;
    r.submittedToTransport = p->dispatched;
    r.error = error;
    r.transportError = transport;
    if (p->reply) {
        r.httpStatus = p->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.retryAfter = p->reply->rawHeader("Retry-After");
        if (r.error == ApiError::None) {
            qsizetype headerSize = 0;
            for (const auto& pair : p->reply->rawHeaderPairs())
                headerSize += pair.first.size() + pair.second.size();
            if (headerSize > 64 * 1024 ||
                (p->reply->rawHeader("Content-Encoding").toLower() != "identity" &&
                 !p->reply->rawHeader("Content-Encoding").isEmpty())) {
                r.error = ApiError::Transport;
                r.transportError = TransportError::ResponseHeadersTooLarge;
            } else if (p->reply->error() != QNetworkReply::NoError || r.httpStatus != 200) {
                r.error = ApiError::Transport;
                r.transportError = TransportError::Network;
            } else {
                r.decoded = decodeResponse(p->body, ResponseEncoding::Json, {16 * 1024, 16 * 1024});
                if (!r.decoded.ok())
                    r.error = ApiError::Decode;
                else if (r.decoded.businessCode == 200 &&
                         r.decoded.object.value(QStringLiteral("size")) !=
                             QJsonValue(QString::number(p->expectedSize)))
                    r.error = ApiError::Decode;
            }
        }
        p->reply->disconnect(this);
        p->reply->abort();
        p->reply->deleteLater();
    }
    emit completed(id, r);
}
} // namespace cirrus::netease
