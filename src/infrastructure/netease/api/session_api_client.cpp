#include "session_api_client.h"

#include "infrastructure/netease/protocol/eapi_request_profile.h"

#include <QDateTime>
#include <QThread>
#include <QTimer>

#include <utility>

namespace cirrus::netease
{

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

void SessionApiClient::invalidatePrevious(domain::SessionToken context)
{
    m_context = context; // credentials already cleared/adopted before any notification
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

quint64 SessionApiClient::submit(domain::SessionToken context, ApiOperation operation,
                                 QJsonObject parameters)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const quint64 id = ++m_nextId;
    Pending pending;
    pending.context = context;
    pending.operation = operation;
    const auto contract = operationContract(operation);
    const auto payload = operationPayload(operation, parameters);
    if (!contract || !payload ||
        (contract->protocol != Protocol::Eapi && contract->protocol != Protocol::Weapi)) {
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
    if (it->operation == ApiOperation::AccountIdentity) {
        const auto musicU = m_cookies.value(it->context, it->origin, "MUSIC_U");
        const auto csrf = m_cookies.value(it->context, it->origin, "__csrf");
        const auto header = m_cookies.header(it->context, it->origin);
        if (!musicU || musicU->isEmpty() || !csrf || !csrf->isValidUtf8() || !header) {
            finish(id, {ApiError::Cookie});
            return;
        }
        it->credentials = m_cookies.snapshot(it->context);
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
        request.responseLimit = it->contract.responseLimit;
        it->transportId = m_transport->post(std::move(request));
        m_transportIds.insert(it->transportId, id);
        it->payload = {};
        return;
    }
    const auto csrf = m_cookies.value(it->context, it->origin, "__csrf");
    const auto musicU = m_cookies.value(it->context, it->origin, "MUSIC_U");
    const auto musicA = m_cookies.value(it->context, it->origin, "MUSIC_A");
    const auto nmtid = m_cookies.value(it->context, it->origin, "NMTID");
    if (!csrf || !musicU || !musicA || !nmtid) {
        finish(id, {ApiError::Cookie});
        return;
    }
    const EapiRequestMetadata metadata{m_deviceId,
                                       QDateTime::currentMSecsSinceEpoch(),
                                       static_cast<quint16>(id % 1000),
                                       *csrf,
                                       *musicU,
                                       *musicA,
                                       *nmtid};
    const auto prepared = prepareEapiRequest(it->contract.logicalPath, it->payload, metadata);
    if (!prepared.ok()) {
        finish(id, {ApiError::Crypto});
        return;
    }
    TransportRequest request;
    request.protocol = it->contract.protocol;
    request.logicalPath = it->contract.logicalPath;
    request.formBody = prepared.formBody;
    request.cookieHeader = prepared.cookieHeader;
    request.userAgent = prepared.userAgent;
    request.responseLimit = it->contract.responseLimit;
    // HttpTransport guarantees deferred completion, so registration cannot race.
    it->transportId = m_transport->post(std::move(request));
    m_transportIds.insert(it->transportId, id);
    it->payload = {};
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
    }
    emit completed(id, pending.context, std::move(result));
}

} // namespace cirrus::netease
