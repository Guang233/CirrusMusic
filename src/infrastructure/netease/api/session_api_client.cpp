#include "session_api_client.h"
#include "comment_write_operation.h"
#include "infrastructure/netease/protocol/comment_v3_policy.h"
#include "logging/durations.h"
#include "logging/log.h"

#include "infrastructure/netease/protocol/client_log.h"
#include "infrastructure/netease/protocol/eapi_request_profile.h"
#include "infrastructure/netease/protocol/xeapi_request_profile.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkCookie>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>

#include <utility>

namespace cirrus::netease
{
bool validAnonymousToken(const QByteArray& token)
{
    if (token.isEmpty() || token.size() > 4096) {
        return false;
    }
    for (const unsigned char c : token) {
        if (c < 0x21 || c > 0x7e || c == ';' || c == ',') {
            return false;
        }
    }
    return true;
}

QByteArray anonymousTokenFrom(const QUrl& origin, const QList<QByteArray>& setCookieHeaders)
{
    const auto now = QDateTime::currentDateTimeUtc();
    QByteArray found;
    for (const auto& header : setCookieHeaders) {
        for (auto cookie : QNetworkCookie::parseCookies(header)) {
            cookie.normalize(origin);
            if (cookie.name() != "MUSIC_A") {
                continue;
            }
            const auto domain = cookie.domain();
            const bool official = domain == QStringLiteral(".music.163.com") ||
                                  domain == origin.host() ||
                                  domain == QLatin1Char('.') + origin.host();
            const bool expired =
                cookie.expirationDate().isValid() && cookie.expirationDate() <= now;
            if (!official || expired || !validAnonymousToken(cookie.value())) {
                return {}; // a suspicious MUSIC_A is never partially trusted
            }
            if (!found.isEmpty() && found != cookie.value()) {
                return {}; // conflicting values: never guess
            }
            found = cookie.value();
        }
    }
    return found;
}

SessionApiClient::~SessionApiClient()
{
    if (!m_stats.empty())
        qCInfo(lcApi) << log::event("api.stats") << m_stats.take();
}

SessionApiClient::SessionApiClient(QString deviceId, QObject* parent)
    : SessionApiClient(std::make_unique<HttpTransport>(), std::move(deviceId), parent)
{
}

SessionApiClient::SessionApiClient(std::unique_ptr<HttpTransport> transport, QString deviceId,
                                   QObject* parent)
    : QObject(parent), m_transport(transport.get()), m_deviceId(std::move(deviceId))
{
    Q_ASSERT(m_transport && !m_transport->parent() && m_transport->thread() == thread());
    m_transport->setParent(this);
    m_transport = transport.release();
    connect(m_transport, &HttpTransport::completed, this, &SessionApiClient::receive);
}

bool SessionApiClient::accepts(const domain::SessionToken& context) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return context.isValid() && context == m_context;
}

bool SessionApiClient::advanceTo(const domain::SessionToken& context)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_cookies.advanceTo(context)) {
        return false;
    }
    if (context == m_context) {
        return true;
    }
    discardReceipt(m_receiptId);
    invalidatePrevious(context);
    return accepts(context);
}

void SessionApiClient::setXeapiKeyRing(XeapiKeyRing* ring)
{
    Q_ASSERT(QThread::currentThread() == thread() && (!ring || ring->thread() == thread()));
    m_xeapiKeys = ring;
}

void SessionApiClient::setAnonymousToken(QByteArray token)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_anonymousToken.fill('\0');
    m_anonymousToken = validAnonymousToken(token) ? std::move(token) : QByteArray();
}

QByteArray SessionApiClient::guestMusicA(const QByteArray& musicU, const QByteArray& musicA) const
{
    // As in the reference: only guests (no MUSIC_U) fall back to the anonymous token.
    return musicU.isEmpty() && musicA.isEmpty() ? m_anonymousToken : musicA;
}

void SessionApiClient::invalidatePrevious(domain::SessionToken context)
{
    m_context = context; // credentials already cleared/adopted before any notification
    if (m_xeapiSession) {
        m_xeapiSession->key.fill('\0');
        m_xeapiSession.reset(); // never carried into another session epoch
    }
    QList<quint64> cancelIds;
    for (auto& pending : m_pending) {
        if (pending.context != context) {
            pending.forcedError = ApiError::StaleContext;
            if (pending.transportId != 0) {
                cancelIds.append(pending.transportId);
            }
        }
    }
    emit contextAdvanced(context);
    // Snapshot IDs: abort/notification may reenter and submit a newer request.
    for (const auto id : cancelIds) {
        m_transport->cancel(id);
    }
}

void SessionApiClient::discardReceipt(const QUuid& receipt)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (receipt == m_receiptId) {
        m_receiptId = {};
        m_receiptCookies.reset();
        m_receiptContext = {};
        m_receiptAge.invalidate();
    }
}

std::optional<QByteArray> SessionApiClient::saveCredentials(domain::SessionToken context,
                                                            QString accountId) const
{
    const auto origin =
        officialRequestUrl(Protocol::Weapi, QStringLiteral("/api/w/nuser/account/get"));
    const auto token = m_cookies.value(context, *origin, "MUSIC_U");
    const auto cookies = m_cookies.encode(context);
    bool validId = false;
    const auto id = accountId.toLongLong(&validId);
    if (!accepts(context) || !token || token->isEmpty() || !cookies || !validId || id <= 0 ||
        QString::number(id) != accountId ||
        !m_cookies.consistentValue(context, "MUSIC_U", *token)) {
        return std::nullopt;
    }
    const auto record = QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                                  {QStringLiteral("deviceId"), m_deviceId},
                                                  {QStringLiteral("accountId"), accountId},
                                                  {QStringLiteral("cookies"),
                                                   QString::fromLatin1(cookies->toBase64())}})
                            .toJson(QJsonDocument::Compact);
    return record.size() <= 192 * 1024 ? std::optional<QByteArray>(record) : std::nullopt;
}

std::optional<QString> SessionApiClient::restoreCredentials(domain::SessionToken context,
                                                            const QByteArray& record)
{
    if (!accepts(context) || !m_pending.isEmpty() || record.size() > 192 * 1024) {
        return std::nullopt;
    }
    const auto object = QJsonDocument::fromJson(record).object();
    const auto idValue = object.value(QStringLiteral("accountId"));
    const auto encoded = object.value(QStringLiteral("cookies"));
    const auto accountId = idValue.toString();
    bool validId = false;
    const auto number = accountId.toLongLong(&validId);
    if (object.size() != 4 || object.value(QStringLiteral("format")) != QJsonValue(1) ||
        object.value(QStringLiteral("deviceId")).toString() != m_deviceId || !idValue.isString() ||
        !validId || number <= 0 || QString::number(number) != accountId || !encoded.isString()) {
        return std::nullopt;
    }
    const auto data = QByteArray::fromBase64Encoding(encoded.toString().toLatin1(),
                                                     QByteArray::AbortOnBase64DecodingErrors);
    SessionCookieStore candidate;
    if (!data || QString::fromLatin1(data.decoded.toBase64()) != encoded.toString() ||
        !candidate.advanceTo(context) || !candidate.restore(context, data.decoded)) {
        return std::nullopt;
    }
    const auto origin =
        officialRequestUrl(Protocol::Weapi, QStringLiteral("/api/w/nuser/account/get"));
    const auto token = candidate.value(context, *origin, "MUSIC_U");
    if (!token || token->isEmpty() || !candidate.consistentValue(context, "MUSIC_U", *token)) {
        return std::nullopt;
    }
    // Copy only after complete schema, scope and credential consistency checks.
    if (!m_cookies.restore(context, data.decoded)) {
        return std::nullopt;
    }
    discardReceipt(m_receiptId);
    return accountId;
}

bool SessionApiClient::promoteReceipt(domain::SessionToken from, domain::SessionToken to,
                                      const QUuid& receipt)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (receipt.isNull() || receipt != m_receiptId) {
        return false;
    }
    const auto cookies = m_receiptCookies;
    const bool valid = accepts(from) && m_receiptContext == from && cookies &&
                       m_receiptAge.isValid() && m_receiptAge.elapsed() <= 60000;
    discardReceipt(receipt); // recognized receipt is consumed even on failed promotion
    const auto origin =
        officialRequestUrl(Protocol::Weapi, QStringLiteral("/api/w/nuser/account/get"));
    const auto musicU = valid ? cookies->value(from, *origin, "MUSIC_U") : std::nullopt;
    if (!musicU || musicU->isEmpty() || !m_cookies.adopt(*cookies, from, to)) {
        return false;
    }
    invalidatePrevious(to);
    return accepts(to); // a reentrant logout invalidates the acknowledgement
}

ApiError SessionApiClient::commentPrerequisite(domain::SessionToken context, ApiOperation op) const
{
    if (!isCommentWriteOperation(op))
        return ApiError::InvalidRequest;
    if (!accepts(context))
        return ApiError::StaleContext;
    const auto contract = commentWriteContract(op);
    const auto origin = officialRequestUrl(contract->protocol, contract->logicalPath);
    const auto musicU = m_cookies.value(context, *origin, "MUSIC_U");
    if (!musicU || musicU->isEmpty())
        return ApiError::Cookie;
    if (contract->protocol == Protocol::Xeapi && (!m_xeapiKeys || !m_xeapiKeys->current()))
        return ApiError::MissingProtocolKey;
    return ApiError::None;
}
quint64 SessionApiClient::submit(domain::SessionToken context, ApiOperation operation,
                                 QJsonObject parameters, QByteArray commentV3Token)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const quint64 id = ++m_nextId;
    Pending pending;
    pending.context = context;
    pending.operation = operation;
    pending.elapsed.start();
    pending.commentV3Token = std::move(commentV3Token);
    const auto contract = operationContract(operation, parameters);
    const auto payload = operationPayload(operation, parameters);
    if ((requiresCommentV3(operation) ? !validCommentV3Token(pending.commentV3Token)
                                      : !pending.commentV3Token.isEmpty()) ||
        !contract || !payload ||
        (contract->protocol != Protocol::Eapi && contract->protocol != Protocol::Weapi &&
         contract->protocol != Protocol::Xeapi &&
         !(contract->protocol == Protocol::Api &&
           contract->destination == OfficialDestination::ClientLog))) {
        pending.forcedError = ApiError::InvalidRequest;
    } else {
        const auto origin = officialRequestUrl(contract->protocol, contract->logicalPath);
        if (!origin) {
            pending.forcedError = ApiError::InvalidRequest;
        } else {
            pending.contract = *contract;
            pending.payload = *payload;
            pending.origin = *origin;
        }
    }
    m_pending.insert(id, std::move(pending));
    QTimer::singleShot(0, this, [this, id] { start(id); });
    return id;
}

void SessionApiClient::start(quint64 id)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return;
    }
    if (!accepts(it->context)) {
        it->forcedError = ApiError::StaleContext;
    }
    if (it->forcedError != ApiError::None) {
        finish(id);
        return;
    }
    if (m_transportIds.size() >= 32) {
        finish(id, {ApiError::Busy});
        return;
    }
    // These lookups use the destination captured from the CLOSED operation
    // registry, never a URL or cookie domain supplied by an API response.
    if (it->contract.protocol == Protocol::Xeapi) {
        startXeapi(id, *it);
        return;
    }
    if (it->contract.destination == OfficialDestination::ClientLog) {
        startClientLog(id, *it);
        return;
    }
    if (it->contract.protocol == Protocol::Weapi) {
        const bool identity = it->operation == ApiOperation::AccountIdentity;
        const auto musicU = m_cookies.value(it->context, it->origin, "MUSIC_U");
        const auto musicA = m_cookies.value(it->context, it->origin, "MUSIC_A");
        const auto csrf = m_cookies.value(it->context, it->origin, "__csrf");
        auto header = m_cookies.header(it->context, it->origin);
        if (!musicU || !musicA || (requiresAccountCookie(it->operation) && musicU->isEmpty()) ||
            !csrf || !csrf->isValidUtf8() || !header) {
            finish(id, {ApiError::Cookie});
            return;
        }
        if (identity) {
            it->credentials = m_cookies.snapshot(it->context);
        } else if (musicU->isEmpty() && musicA->isEmpty() && !m_anonymousToken.isEmpty()) {
            if (!header->isEmpty())
                header->append("; ");
            header->append("MUSIC_A=" + m_anonymousToken);
        }
        // The heart answers -460 to a request without a client's os and appver
        // (docs/api/collection-writes.md §1a): those of the Pc eapi profile.
        if (it->operation == ApiOperation::SongHeart) {
            if (!header->isEmpty())
                header->append("; ");
            header->append("os=pc; appver=3.1.17.204416");
        }
        auto payload = it->payload;
        payload.insert(QStringLiteral("e_r"), false);
        payload.insert(QStringLiteral("csrf_token"), QString::fromUtf8(*csrf));
        const auto encrypted = encodeWeapi(payload);
        if (!encrypted.ok()) {
            finish(id, {ApiError::Crypto});
            return;
        }
        TransportRequest request;
        request.protocol = Protocol::Weapi;
        request.logicalPath = it->contract.logicalPath;
        request.formBody = encodeForm(encrypted.fields);
        request.cookieHeader = *header;
        request.userAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                            "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0";
        request.musicReferer = true;
        request.destination = it->contract.destination;
        request.responseLimit = it->contract.responseLimit;
        request.retryLostConnection = retriesLostConnection(it->operation);
        it->transportId = m_transport->post(std::move(request));
        m_transportIds.insert(it->transportId, id);
        it->payload = {};
        return;
    }
    const auto csrf = m_cookies.value(it->context, it->origin, "__csrf");
    const auto musicU = m_cookies.value(it->context, it->origin, "MUSIC_U");
    const auto musicA = m_cookies.value(it->context, it->origin, "MUSIC_A");
    const auto nmtid = m_cookies.value(it->context, it->origin, "NMTID");
    if (!csrf || !musicU || !musicA || !nmtid ||
        (requiresAccountCookie(it->operation) && musicU->isEmpty())) {
        finish(id, {ApiError::Cookie});
        return;
    }
    const EapiRequestMetadata metadata{m_deviceId,
                                       QDateTime::currentMSecsSinceEpoch(),
                                       static_cast<quint16>(id % 1000),
                                       *csrf,
                                       *musicU,
                                       guestMusicA(*musicU, *musicA),
                                       *nmtid};
    auto profile = metadata;
    profile.encryptedResponse = it->operation == ApiOperation::SearchPodcasts;
    const auto prepared = prepareEapiRequest(it->contract.logicalPath, it->payload, profile);
    if (!prepared.ok()) {
        finish(id, {ApiError::Crypto});
        return;
    }
    TransportRequest request;
    request.protocol = it->contract.protocol;
    request.destination = it->contract.destination;
    request.logicalPath = it->contract.logicalPath;
    request.formBody = prepared.formBody;
    request.cookieHeader = prepared.cookieHeader;
    request.userAgent = prepared.userAgent;
    request.responseLimit = it->contract.responseLimit;
    // HttpTransport guarantees deferred completion, so registration cannot race.
    request.retryLostConnection = retriesLostConnection(it->operation);
    it->transportId = m_transport->post(std::move(request));
    m_transportIds.insert(it->transportId, id);
    it->payload = {};
}

void SessionApiClient::startClientLog(quint64 id, Pending& pending)
{
    const auto csrf = m_cookies.value(pending.context, pending.origin, "__csrf");
    const auto musicU = m_cookies.value(pending.context, pending.origin, "MUSIC_U");
    const auto nmtid = m_cookies.value(pending.context, pending.origin, "NMTID");
    if (!csrf || !musicU || musicU->isEmpty() || !nmtid) {
        finish(id, {ApiError::Cookie});
        return;
    }
    // The Android client's identity, as our stream URL requests (xeapi) carry it: its
    // log header holds MUSIC_U, appver and buildver. The device id must be in its format.
    const auto buildver = QByteArray::number(QDateTime::currentSecsSinceEpoch());
    QJsonObject meta{{QStringLiteral("MUSIC_U"), QString::fromUtf8(*musicU)},
                     {QStringLiteral("appver"), QStringLiteral("9.1.65")},
                     {QStringLiteral("buildver"), QString::fromLatin1(buildver)}};
    const auto device = androidDeviceId(m_deviceId); // not our PC-format id (dropped)
    QByteArray header = "MUSIC_U=" + QUrl::toPercentEncoding(QString::fromUtf8(*musicU));
    if (!csrf->isEmpty())
        header += "; __csrf=" + QUrl::toPercentEncoding(QString::fromUtf8(*csrf));
    if (!nmtid->isEmpty())
        header += "; NMTID=" + QUrl::toPercentEncoding(QString::fromUtf8(*nmtid));
    header += "; os=android; osver=16; appver=9.1.65; buildver=" + buildver +
              "; channel=netease; deviceId=" + device + "; sDeviceId=" + device;
    // As the official client uploads (§4d): one file per stream (`flush_ua_…` for the
    // data-report SDK's events, `flush_bi_…` for BI logs), each record with the fields
    // that SDK / the BI logger add to every log.
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_logPid == 0)
        m_logPid = 10000 + QRandomGenerator::global()->bounded(20000);
    if (m_logSession.isEmpty())
        m_logSession = QString::number(now) + u'#' +
                       QString::number(100 + QRandomGenerator::global()->bounded(900)) +
                       u"#9.1.65#" + QString::fromLatin1(buildver);
    QList<ClientLogFile> files;
    QStringList actions;
    QHash<QString, qsizetype> fileOf;
    for (const auto& entry : pending.payload.value(QStringLiteral("records")).toArray()) {
        const auto row = entry.toObject();
        const auto stream = row.value(QStringLiteral("stream")).toString();
        const auto action = row.value(QStringLiteral("action")).toString();
        if (stream != u"ua" && stream != u"bi") {
            finish(id, {ApiError::InvalidRequest});
            return;
        }
        auto data = row.value(QStringLiteral("data")).toObject();
        for (const auto& [key, value] : std::initializer_list<std::pair<const char*, QJsonValue>>{
                 {"_logTime", now},
                 {"_log_thoroughfare", stream},
                 {"abtest", QString()},
                 {"buildType", QStringLiteral("release")},
                 {"lca", 1},
                 {"libra_abt", QString()},
                 {"net_wbid", QString()},
                 {"net_wn", QString()},
                 {"netstatus", QStringLiteral("wifi")},
                 {"pid", m_logPid},
                 {"screenType", QStringLiteral("other")}})
            data.insert(QString::fromLatin1(key), value);
        if (stream == u"ua") {
            data.insert(QStringLiteral("_sessid"), m_logSession);
            data.insert(QStringLiteral("_sidrefer"), m_logSession);
            data.insert(QStringLiteral("_hsrefer"), QString());
            data.insert(QStringLiteral("g_dprefer"), QString());
            data.insert(QStringLiteral("channel"), QStringLiteral("netease"));
            data.insert(QStringLiteral("logtime"), now);
            data.insert(QStringLiteral("process_id"), m_logPid);
            if (action == u"_pld") { // the app's install / update times (ms)
                const auto installed = m_logSession.section(u'#', 0, 0).toLongLong();
                data.insert(QStringLiteral("firstInstallTime"), installed);
                data.insert(QStringLiteral("updateTime"), installed);
            }
        }
        auto it = fileOf.find(stream);
        if (it == fileOf.end()) {
            const auto name = "flush_" + stream.toLatin1() + '_' + QByteArray::number(m_logPid) +
                              '_' + QByteArray::number(++m_logSequence) + '_' +
                              QByteArray::number(QRandomGenerator::global()->generate());
            it = fileOf.insert(stream, files.size());
            files.append({name, {}});
        }
        actions.append(action);
        files[*it].records += clientLogRecord(now / 1000, action.toLatin1(), data);
    }
    const auto upload = clientLogUpload(QJsonDocument(meta).toJson(QJsonDocument::Compact), files);
    if (!upload.ok()) {
        finish(id, {ApiError::Crypto});
        return;
    }
    qCInfo(lcReport) << log::event("report.upload") << log::field("req", id)
                     << log::field("actions", actions.join(u','))
                     << log::field("files", QString::fromLatin1(upload.fileNames.join(',')))
                     << log::field("bytes", upload.body.size());
    TransportRequest request;
    request.protocol = pending.contract.protocol;
    request.destination = pending.contract.destination;
    request.logicalPath = pending.contract.logicalPath;
    request.formBody = upload.body;
    request.contentType = upload.contentType;
    request.cookieHeader = header;
    request.userAgent = "NeteaseMusic/9.5.61.260802021928(9005061);Dalvik/2.1.0 (Linux; U; "
                        "Android 12; HBN-AL00 Build/cd737a2.0)"; // as the xeapi profile
    request.responseLimit = pending.contract.responseLimit;
    pending.uploadFiles = upload.fileNames;
    pending.transportId = m_transport->post(std::move(request));
    m_transportIds.insert(pending.transportId, id);
    pending.payload = {};
}

void SessionApiClient::startXeapi(quint64 id, Pending& pending)
{
    const auto key = m_xeapiKeys ? m_xeapiKeys->current() : std::nullopt;
    if (!key) {
        finish(id, {ApiError::MissingProtocolKey}); // never downgrade to another protocol
        return;
    }
    const auto csrf = m_cookies.value(pending.context, pending.origin, "__csrf");
    const auto musicU = m_cookies.value(pending.context, pending.origin, "MUSIC_U");
    const auto musicA = m_cookies.value(pending.context, pending.origin, "MUSIC_A");
    const auto nmtid = m_cookies.value(pending.context, pending.origin, "NMTID");
    if (!csrf || !musicU || !musicA || !nmtid ||
        (requiresAccountCookie(pending.operation) && musicU->isEmpty())) {
        finish(id, {ApiError::Cookie});
        return;
    }
    auto payload = pending.payload;
    if (pending.operation == ApiOperation::AnonymousRegister) {
        payload.insert(QStringLiteral("username"), anonymousRegistrationUsername(m_deviceId));
    }
    const auto fields = xeapiFormFields(payload);
    const XeapiRequestMetadata metadata{m_deviceId, QDateTime::currentMSecsSinceEpoch(), *csrf,
                                        *musicU,    guestMusicA(*musicU, *musicA),       *nmtid};
    const auto prepared = fields ? prepareXeapiRequest(*fields, *key, m_xeapiSession, metadata)
                                 : PreparedXeapiRequest{CryptoError::InvalidInput};
    if (!prepared.ok()) {
        finish(id, {ApiError::Crypto});
        return;
    }
    TransportRequest request;
    request.protocol = Protocol::Xeapi;
    request.logicalPath = pending.contract.logicalPath;
    request.formBody = prepared.formBody;
    request.cookieHeader = prepared.cookieHeader;
    request.userAgent = prepared.userAgent;
    request.xeapiHeaders = prepared.headers;
    request.commentV3Token = std::move(pending.commentV3Token);
    request.responseLimit = pending.contract.responseLimit;
    request.retryLostConnection = retriesLostConnection(pending.operation);
    pending.transportId = m_transport->post(std::move(request));
    m_transportIds.insert(pending.transportId, id);
    pending.payload = {};
}

void SessionApiClient::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return;
    }
    if (it->forcedError != ApiError::StaleContext) {
        it->forcedError = ApiError::Canceled;
    }
    if (it->transportId != 0) {
        m_transport->cancel(it->transportId);
    }
    // Pre-start cancellation is completed by the queued start callback.
}

void SessionApiClient::receive(quint64 transportId, const TransportResult& transport)
{
    const auto id = m_transportIds.value(transportId);
    const auto it = m_pending.constFind(id);
    if (it == m_pending.constEnd()) {
        return;
    }
    const auto pending = *it;
    if (!accepts(pending.context) || pending.forcedError != ApiError::None) {
        finish(id);
        return;
    }
    ApiResult result;
    result.transportError = transport.error;
    result.httpStatus = transport.httpStatus;
    result.retryAfter = transport.retryAfter;
    if (transport.error != TransportError::None) {
        result.error = ApiError::Transport;
        finish(id, std::move(result));
        return;
    }
    result.decoded =
        decodeResponse(transport.body, pending.contract.responseEncoding,
                       {pending.contract.responseLimit, pending.contract.responseLimit});
    if (result.decoded.ok() && pending.contract.protocol == Protocol::Xeapi &&
        !transport.xeapiSessionId.isEmpty()) {
        // Adopted only for the still-current epoch (checked above); invalid values ignored.
        const XeapiSessionKey session{transport.xeapiSessionId, transport.xeapiSessionKey};
        if (session.isValid()) {
            m_xeapiSession = session;
        }
    }
    if (result.decoded.ok() && pending.operation == ApiOperation::AnonymousRegister &&
        result.decoded.businessCode == 200) {
        // Returned to the caller, never staged into the account jar.
        result.anonymousToken = anonymousTokenFrom(pending.origin, transport.setCookieHeaders);
    }
    if (result.decoded.ok() && !pending.uploadFiles.isEmpty() &&
        result.decoded.businessCode == 200) {
        // Taken only when the service lists our file (§4d); otherwise a rejection.
        const auto files = result.decoded.object.value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("successfiles"))
                               .toArray();
        bool taken = true;
        for (const auto& name : pending.uploadFiles)
            taken = taken && files.contains(QString::fromLatin1(name));
        qCInfo(lcReport) << log::event("report.upload_result") << log::field("req", id)
                         << log::field("taken", taken) << log::field("files", files.size())
                         << log::field("rate", result.decoded.object.value(QStringLiteral("data"))
                                                   .toObject()
                                                   .value(QStringLiteral("rate"))
                                                   .toInt(-1));
        if (!taken)
            result.decoded.businessCode = -2;
    }
    if (!result.decoded.ok()) {
        result.error = ApiError::Decode;
    } else if (acceptsResponseCookies(pending.operation, result.decoded)) {
        auto* cookies = pending.credentials ? pending.credentials.get() : &m_cookies;
        if (cookies->apply(pending.context, pending.origin, transport.setCookieHeaders) !=
            CookieUpdate::Applied) {
            result.error = ApiError::Cookie;
        } else if (pending.credentials) {
            const auto musicU = cookies->value(pending.context, pending.origin, "MUSIC_U");
            if (!musicU || musicU->isEmpty() ||
                !cookies->consistentValue(pending.context, "MUSIC_U", *musicU)) {
                result.error = ApiError::Cookie;
            } else {
                m_receiptId = QUuid::createUuid();
                m_receiptContext = pending.context;
                m_receiptCookies = pending.credentials;
                m_receiptAge.start();
                result.credentialReceipt = m_receiptId;
            }
        }
    }
    if (result.error == ApiError::None && result.decoded.businessCode == 200 &&
        (pending.operation == ApiOperation::SongDetails ||
         pending.operation == ApiOperation::SongLyrics))
        result.cacheRetention = transport.cacheRetention;
    finish(id, std::move(result));
}

void SessionApiClient::finish(quint64 id, ApiResult result)
{
    if (!m_pending.contains(id)) {
        return;
    }
    const auto pending = m_pending.take(id);
    m_transportIds.remove(pending.transportId);
    const auto candidateReceipt = result.credentialReceipt;
    const auto code = result.decoded.businessCode;
    // The provider's own explanation of a code other than 200 ("操作频繁"), for the log
    // only: taken before failed results are emptied below.
    QString providerMessage;
    if (code && *code != 200)
        for (const char* key : {"message", "msg"})
            if (const QJsonValue v = result.decoded.object.value(QLatin1StringView(key));
                v.isString()) {
                providerMessage = v.toString();
                break;
            }
    if (!accepts(pending.context)) {
        result = {ApiError::StaleContext};
    } else if (pending.forcedError != ApiError::None) {
        result = {pending.forcedError};
    }
    if (result.error != ApiError::None) {
        if (!candidateReceipt.isNull()) {
            discardReceipt(candidateReceipt);
        }
        result.credentialReceipt = {};
        result.decoded.object = {}; // never leak partial/stale provider data
        result.decoded.businessCode.reset();
        result.cacheRetention = {};
    }
    result.submittedToTransport = pending.transportId != 0;
    // Every operation's outcome (docs/tasks/logging.md §8): the provider's code, whatever
    // it means for this operation (QR polling answers 800-803, for one). Transport
    // failures were logged by the transport.
    const QString path = pending.contract.logicalPath.isEmpty()
                             ? QStringLiteral("op#") + QString::number(int(pending.operation))
                             : pending.contract.logicalPath;
    const qint64 ms = pending.elapsed.elapsed();
    switch (result.error) {
    case ApiError::Canceled:
    case ApiError::StaleContext:
        break;
    case ApiError::None:
        m_stats.add(ms, path);
        break;
    case ApiError::Transport:
        m_stats.add(ms, path, log::nameOf(result.transportError));
        break;
    default:
        m_stats.add(ms, path, log::nameOf(result.error));
        break;
    }
    if (m_stats.due())
        qCInfo(lcApi) << log::event("api.stats") << m_stats.take();
    switch (result.error) {
    case ApiError::None:
        qCDebug(lcApi) << log::event("api.done") << log::field("req", pending.transportId)
                       << log::field("path", path)
                       << log::field("code", code ? QString::number(*code) : QStringLiteral("-"))
                       << log::fieldIf(!providerMessage.isEmpty(), "msg",
                                       log::message(providerMessage))
                       << log::field("ms", ms);
        break;
    case ApiError::Canceled:
    case ApiError::StaleContext:
    case ApiError::Transport:
        qCDebug(lcApi) << log::event("api.ended") << log::field("req", pending.transportId)
                       << log::field("path", path) << log::field("err", result.error)
                       << log::field("transport", result.transportError) << log::field("ms", ms);
        break;
    default:
        qCWarning(lcApi) << log::event("api.failed") << log::field("req", pending.transportId)
                         << log::field("path", path) << log::field("err", result.error)
                         << log::fieldIf(result.error == ApiError::Decode, "decode",
                                         result.decoded.error)
                         << log::fieldIf(code.has_value(), "code", code.value_or(0))
                         << log::fieldIf(!providerMessage.isEmpty(), "msg",
                                         log::message(providerMessage))
                         << log::fieldIf(result.httpStatus != 0, "status", result.httpStatus)
                         << log::field("ms", ms);
        break;
    }
    emit completed(id, pending.context, std::move(result));
}

} // namespace cirrus::netease
