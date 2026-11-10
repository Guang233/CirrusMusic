#include "http_transport.h"
#include "infrastructure/network/cache_retention_policy.h"
#include "logging/log.h"
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

// A read whose connection closed before any response is sent again: at once over
// HTTP/1.1, then, closed again, once more a second later (2026-11-10: behind a TUN
// proxy fresh connections to NetEase are closed, and the immediate resend too, about
// one time in three; a second later it goes through).
constexpr int kLostConnectionResends = 2;
constexpr int kSecondResendDelayMs = 1000;

} // namespace

struct HttpTransport::Pending {
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> timer;
    QByteArray body;
    qsizetype limit = 0;
    QElapsedTimer elapsed;
    bool xeapi = false;
    TransportError forcedError = TransportError::None;
    // For the log: the operation's path (no query), never the URL or the body.
    QString path;
    Protocol protocol = Protocol::Eapi;
    // retryLostConnection: the request to send again, and how many times it was.
    std::optional<TransportRequest> again;
    int resends = 0;
    bool waiting = false; // between a lost connection and the delayed resend
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
    pending->path = request.logicalPath.left(256);
    pending->protocol = request.protocol;
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
    if (request.retryLostConnection && pending->resends < kLostConnectionResends)
        pending->again = request;
    QNetworkRequest wire(*url);
    if (pending->resends > 0) // the HTTP/2 connection was lost: this time over HTTP/1.1
        wire.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
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
    } else if (pending->waiting) {
        // Waiting to be sent again: done now rather than when the delay is up (the
        // delayed start then finds nothing).
        QTimer::singleShot(0, this, [this, id] { finish(id); });
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
    // A read whose connection closed before any response: again, over HTTP/1.1
    // (kLostConnectionResends).
    if (pending->again && pending->forcedError == TransportError::None && pending->reply &&
        pending->body.isEmpty() && pending->reply->bytesAvailable() == 0 &&
        pending->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0 &&
        (pending->reply->error() == QNetworkReply::ProtocolFailure ||
         pending->reply->error() == QNetworkReply::RemoteHostClosedError)) {
        const int delayMs = pending->resends == 0 ? 0 : kSecondResendDelayMs;
        qCWarning(lcNet) << log::event("request.retry") << log::field("req", id)
                         << log::field("path", pending->path)
                         << log::field("qt", pending->reply->error())
                         << log::field("attempt", pending->resends + 1)
                         << log::field("delay_ms", delayMs)
                         << log::field("ms", pending->elapsed.elapsed());
        auto request = *std::exchange(pending->again, std::nullopt);
        ++pending->resends;
        pending->waiting = true;
        pending->reply->disconnect(this);
        pending->reply->deleteLater(); // its timer goes with it
        pending->reply.clear();
        m_pending.insert(id, pending);
        QTimer::singleShot(delayMs, this, [this, id, request = std::move(request)] {
            if (const auto waiting = m_pending.value(id))
                waiting->waiting = false;
            start(id, request);
        });
        return;
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
    // One record per request (docs/tasks/logging.md §8): failures as warnings, with what
    // Qt said; the rest in detail mode. The API client adds the operation's outcome.
    const bool h2 = pending->reply &&
                    pending->reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
    if (result.error == TransportError::None)
        qCDebug(lcNet) << log::event("request.done") << log::field("req", id)
                       << log::field("path", pending->path)
                       << log::field("proto", pending->protocol)
                       << log::field("status", result.httpStatus)
                       << log::field("bytes", result.body.size())
                       << log::field("ms", pending->elapsed.elapsed()) << log::field("h2", h2);
    else if (result.error == TransportError::Canceled)
        qCDebug(lcNet) << log::event("request.canceled") << log::field("req", id)
                       << log::field("path", pending->path)
                       << log::field("ms", pending->elapsed.elapsed());
    else
        qCWarning(lcNet) << log::event("request.failed") << log::field("req", id)
                         << log::field("path", pending->path)
                         << log::field("proto", pending->protocol)
                         << log::field("err", result.error)
                         << log::field("qt", QNetworkReply::NetworkError(result.networkError))
                         << log::field("status", result.httpStatus)
                         << log::field("ms", pending->elapsed.elapsed()) << log::field("h2", h2)
                         << log::field("retry_after", QString::fromLatin1(result.retryAfter));
    emit completed(id, result);
}

} // namespace cirrus::netease
