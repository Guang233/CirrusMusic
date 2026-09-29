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
    m_context = context; // credential clearing precedes notification/new requests
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
    return accepts(context); // a reentrant newer transition cannot acknowledge this one
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
    if (!contract || !payload || contract->protocol != Protocol::Eapi) {
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
    } else if (acceptsResponseCookies(pending.operation, result.decoded) &&
               m_cookies.apply(pending.context, pending.origin, transport.setCookieHeaders) !=
                   CookieUpdate::Applied) {
        result.error = ApiError::Cookie;
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
    if (!accepts(pending.context)) {
        result = {ApiError::StaleContext};
    } else if (pending.forcedError != ApiError::None) {
        result = {pending.forcedError};
    }
    if (result.error != ApiError::None) {
        result.decoded.object = {}; // never leak partial/stale provider data
        result.decoded.businessCode.reset();
    }
    emit completed(id, pending.context, std::move(result));
}

} // namespace cirrus::netease
