#include "http_transport.h"
#include "infrastructure/network/cache_retention_policy.h"
#include <QElapsedTimer>

#include "infrastructure/netease/protocol/comment_v3_policy.h"
#include "infrastructure/netease/protocol/xeapi_request_profile.h"

#include <QHttpHeaders>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <utility>

namespace cirrus::netease
{
namespace
{

bool validHeaderValue(const QByteArray& value, qsizetype limit)
{
    if (value.size() > limit) {
        return false;
    }
    for (const unsigned char c : value) {
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

bool collectCookies(const QNetworkReply& reply, QList<QByteArray>& cookies)
{
    qsizetype bytes = 0;
    // The legacy rawHeaderPairs() view coalesces duplicates. QHttpHeaders keeps
    // original Set-Cookie boundaries; never split on commas (Expires uses them).
    for (const auto& value : reply.headers().values(QHttpHeaders::WellKnownHeader::SetCookie)) {
        bytes += value.size();
        if (cookies.size() >= 64 || value.size() > 4096 || bytes > 64 * 1024) {
            cookies.clear();
            return false;
        }
        cookies.append(value);
    }
    return true;
}

bool validXeapiHeaders(const TransportRequest& request)
{
    if (request.xeapiHeaders.isEmpty()) {
        return true;
    }
    if (request.protocol != Protocol::Xeapi || request.xeapiHeaders.size() > 16) {
        return false;
    }
    QList<QByteArray> seen;
    for (const auto& [name, value] : request.xeapiHeaders) {
        if (!isXeapiProfileHeader(name) || seen.contains(name) || value.isEmpty() ||
            !validHeaderValue(value, 4096)) {
            return false;
        }
        seen.append(name);
    }
    return true;
}

} // namespace

struct HttpTransport::Pending {
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> timer;
    QByteArray body;
    qsizetype limit = 0;
    QElapsedTimer elapsed;
    bool xeapi = false;
    TransportError forcedError = TransportError::None;
};

HttpTransport::HttpTransport(QObject* parent)
    : HttpTransport(std::make_unique<QNetworkAccessManager>(), parent)
{
}

HttpTransport::HttpTransport(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent)
    : QObject(parent), m_manager(manager.get())
{
    Q_ASSERT(m_manager && !m_manager->parent() && m_manager->thread() == thread());
    // One owner: QObject parenting also tracks a later moveToThread.
    m_manager->setParent(this);
    m_manager = manager.release();
}

HttpTransport::~HttpTransport()
{
    // Abort without emitting results into objects being torn down.
    for (const auto& pending : std::as_const(m_pending)) {
        if (pending->reply) {
            pending->reply->disconnect(this);
            if (pending->timer) {
                pending->timer->stop();
                pending->timer->disconnect(this);
            }
            pending->reply->abort();
        }
    }
}

quint64 HttpTransport::post(TransportRequest request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const quint64 id = ++m_nextId;
    auto pending = std::make_shared<Pending>();
    pending->limit = request.responseLimit;
    pending->elapsed.start();
    m_pending.insert(id, pending);
    // Always deferred: even validation failure/cancel-before-start can be observed
    // after post() returns. Callers must keep a receiver connected until completion.
    QTimer::singleShot(0, this, [this, id, request = std::move(request)] { start(id, request); });
    return id;
}

void HttpTransport::start(quint64 id, const TransportRequest& request)
{
    const auto pending = m_pending.value(id);
    if (!pending) {
        return;
    }
    if (pending->forcedError != TransportError::None) {
        finish(id);
        return;
    }
    const auto url = officialRequestUrl(request.protocol, request.logicalPath, request.destination);
    if (!url || request.logicalPath.size() > 2048 || request.timeoutMs <= 0 ||
        request.timeoutMs > 120000 || request.responseLimit <= 0 ||
        request.responseLimit > 32 * 1024 * 1024 || request.formBody.size() > 2 * 1024 * 1024 ||
        !validHeaderValue(request.cookieHeader, 16 * 1024) ||
        !validHeaderValue(request.userAgent, 1024) ||
        (request.musicReferer && request.protocol != Protocol::Weapi) ||
        !validXeapiHeaders(request) ||
        (commentV3Path(request.protocol, request.logicalPath)
             ? !validCommentV3Token(request.commentV3Token)
             : !request.commentV3Token.isEmpty()) ||
        (!request.commentV3Token.isEmpty() &&
         request.destination != OfficialDestination::Default)) {
        pending->forcedError = TransportError::InvalidRequest;
        finish(id);
        return;
    }
    pending->xeapi = request.protocol == Protocol::Xeapi;
    QNetworkRequest wire(*url);
    wire.setHeader(QNetworkRequest::ContentTypeHeader,
                   QStringLiteral("application/x-www-form-urlencoded;charset=utf-8"));
    wire.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::ManualRedirectPolicy);
    wire.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    wire.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    if (!request.cookieHeader.isEmpty()) {
        wire.setRawHeader("Cookie", request.cookieHeader);
    }
    if (!request.userAgent.isEmpty()) {
        wire.setRawHeader("User-Agent", request.userAgent);
    }

    if (request.musicReferer) {
        wire.setRawHeader("Referer", "https://music.163.com");
    }
    for (const auto& [name, value] : request.xeapiHeaders) {
        wire.setRawHeader(name, value);
    }
    if (!request.commentV3Token.isEmpty())
        wire.setRawHeader("X-antiCheatToken", request.commentV3Token);
    auto* reply = m_manager->post(wire, request.formBody);
    pending->reply = reply;
    reply->setReadBufferSize(64 * 1024);
    pending->timer = new QTimer(reply);
    pending->timer->setSingleShot(true);
    connect(pending->timer, &QTimer::timeout, this,
            [this, id] { terminate(id, TransportError::Timeout); });
    connect(reply, &QNetworkReply::readyRead, this, [this, id] { consume(id); });
    connect(reply, &QNetworkReply::finished, this, [this, id] { finish(id); });
    pending->timer->start(request.timeoutMs);
    // Also support injected replies which have already finished synchronously.
    if (reply->isFinished()) {
        finish(id);
    }
}

bool HttpTransport::drain(const std::shared_ptr<Pending>& pending)
{
    if (!pending->reply || pending->forcedError != TransportError::None) {
        return true;
    }
    const qsizetype remaining = pending->limit - pending->body.size();
    const auto bytes = pending->reply->read(remaining + 1);
    if (bytes.size() > remaining) {
        pending->body.clear();
        pending->forcedError = TransportError::ResponseTooLarge;
        return false;
    }
    pending->body.append(bytes);
    return true;
}

void HttpTransport::consume(quint64 id)
{
    const auto pending = m_pending.value(id);
    if (pending && !drain(pending)) {
        // abort may synchronously emit finished; do not use map references here.
        pending->reply->abort();
    }
}

void HttpTransport::terminate(quint64 id, TransportError error)
{
    const auto pending = m_pending.value(id);
    if (!pending || pending->forcedError != TransportError::None) {
        return;
    }
    pending->forcedError = error;
    if (pending->reply) {
        pending->reply->abort();
    }
    // A pre-start cancellation is completed by the queued start callback.
}

void HttpTransport::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    terminate(id, TransportError::Canceled);
}

void HttpTransport::finish(quint64 id)
{
    const auto pending = m_pending.take(id);
    if (!pending) {
        return; // terminal callback is delivered at most once
    }
    if (pending->timer) {
        pending->timer->stop();
    }
    drain(pending);
    TransportResult result;
    result.error = pending->forcedError;
    result.body = std::move(pending->body);
    if (pending->reply) {
        result.httpStatus =
            pending->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.networkError = pending->reply->error();
        result.retryAfter = pending->reply->rawHeader("Retry-After").left(256);
        if (result.error == TransportError::None) {
            if (result.httpStatus >= 300 && result.httpStatus < 400) {
                result.error = TransportError::RedirectBlocked;
            } else if (result.httpStatus >= 400) {
                result.error = TransportError::HttpStatus;
            } else if (result.networkError != QNetworkReply::NoError || result.httpStatus == 0) {
                result.error = TransportError::Network;
            } else if (result.httpStatus < 200 || result.httpStatus >= 300) {
                result.error = TransportError::HttpStatus;
            }
        }
        // Failed/canceled/redirected responses cannot hand credentials to callers.
        if (result.error == TransportError::None &&
            !collectCookies(*pending->reply, result.setCookieHeaders)) {
            result.error = TransportError::ResponseHeadersTooLarge;
            result.body.clear();
        }
        if (result.error == TransportError::None && pending->xeapi) {
            auto sessionId = pending->reply->rawHeader("x-encr-ssid");
            auto sessionKey = pending->reply->rawHeader("x-encr-sskey");
            if (!sessionId.isEmpty() && !sessionKey.isEmpty() && validHeaderValue(sessionId, 256) &&
                validHeaderValue(sessionKey, 256)) {
                result.xeapiSessionId = std::move(sessionId);
                result.xeapiSessionKey = std::move(sessionKey);
            }
        }
        if (result.error == TransportError::None)
            result.cacheRetention =
                network::cacheRetention(*pending->reply, pending->elapsed.elapsed());
        pending->reply->deleteLater();
    }
    emit completed(id, result);
}

} // namespace cirrus::netease
