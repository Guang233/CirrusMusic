#include "media_buffer.h"
#include "media_source_policy.h"
#include <QNetworkReply>
#include <QThread>

namespace cirrus::media
{
using namespace domain;
MediaBuffer::MediaBuffer(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent)
    : QObject(parent),
      m_manager(manager ? std::move(manager) : std::make_unique<QNetworkAccessManager>())
{
    Q_ASSERT(m_manager->thread() == thread() && !m_manager->parent());
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(m_source.validUntil.hasExpired() ? PlaybackError::Expired : PlaybackError::Timeout);
    });
}
MediaBuffer::~MediaBuffer() { cancel(); }
void MediaBuffer::retireReply()
{
    auto* reply = m_reply.data();
    m_reply = nullptr;
    if (!reply)
        return;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}
void MediaBuffer::cancel()
{
    m_active = false;
    m_deadline.stop();
    retireReply();
    m_file.reset();
    m_source = {};
}
void MediaBuffer::start(MediaSource source, LoadToken token)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancel();
    m_source = std::move(source);
    m_token = token;
    m_active = true;
    m_bytes = 0;
    m_redirects = 0;
    // Local failures are deferred so callers can finish installing their load context.
    QTimer::singleShot(0, this, [this, token] {
        if (!m_active || m_token != token)
            return;
        if (!approvedMediaUrl(m_source.url)) {
            fail(PlaybackError::UnsafeSource);
            return;
        }
        if (m_source.validUntil.isForever() || m_source.validUntil.hasExpired()) {
            fail(PlaybackError::Expired);
            return;
        }
        if (m_source.size <= 0 || m_source.size > MaximumBytes) {
            fail(PlaybackError::TooLarge);
            return;
        }
        m_file = std::make_unique<QTemporaryFile>(); // 0600, auto-remove; never persisted as
                                                     // library/cache
        if (!m_file->open()) {
            fail(PlaybackError::Storage);
            return;
        }
        m_deadline.start(int(qMin<qint64>(45000, m_source.validUntil.remainingTime())));
        request(m_source.url);
    });
}
void MediaBuffer::request(QUrl url)
{
    if (!approvedMediaUrl(url)) {
        fail(PlaybackError::UnsafeSource);
        return;
    }
    if (m_source.validUntil.hasExpired()) {
        fail(PlaybackError::Expired);
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setTransferTimeout(15000);
    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(128 * 1024);
    connect(m_reply, &QIODevice::readyRead, this, &MediaBuffer::consume);
    connect(m_reply, &QNetworkReply::finished, this, &MediaBuffer::finish);
    // Qt's default TLS verification is retained; no sslErrors bypass or auto-auth handlers.
}
void MediaBuffer::consume()
{
    if (!m_active || !m_reply ||
        m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
        return;
    const auto length = m_reply->rawHeader("Content-Length");
    bool ok = false;
    const auto n = length.toLongLong(&ok);
    if (!length.isEmpty() && (!ok || n != m_source.size || n > MaximumBytes)) {
        fail(PlaybackError::TooLarge);
        return;
    }
    const auto encoding = m_reply->rawHeader("Content-Encoding").toLower();
    if (!encoding.isEmpty() && encoding != "identity") {
        fail(PlaybackError::InvalidResponse);
        return;
    }
    while (m_reply && m_reply->bytesAvailable() > 0) {
        const auto chunk = m_reply->read(64 * 1024);
        if (chunk.isEmpty())
            break;
        m_bytes += chunk.size();
        if (m_bytes > m_source.size || m_bytes > MaximumBytes) {
            fail(PlaybackError::TooLarge);
            return;
        }
        if (m_file->write(chunk) != chunk.size()) {
            fail(PlaybackError::Storage);
            return;
        }
    }
}
void MediaBuffer::finish()
{
    if (!m_active || !m_reply)
        return;
    const auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const auto location = m_reply->rawHeader("Location");
        const auto url = m_reply->url().resolved(QUrl::fromEncoded(location, QUrl::StrictMode));
        if (++m_redirects > 3 || location.isEmpty() || location.size() > 8192 ||
            !approvedMediaUrl(url)) {
            fail(PlaybackError::UnsafeSource);
            return;
        }
        retireReply();
        request(url);
        return;
    }
    if (status != 200 || m_reply->error() != QNetworkReply::NoError) {
        fail(status == 401 || status == 403 ? PlaybackError::Unavailable
             : status == 429                ? PlaybackError::RateLimited
                                            : PlaybackError::Network);
        return;
    }
    consume();
    if (!m_active || !m_reply)
        return;
    if (m_bytes != m_source.size || !m_file->flush() || !m_file->seek(0)) {
        fail(PlaybackError::InvalidResponse);
        return;
    }
    m_deadline.stop();
    retireReply();
    m_source.url = QUrl{}; // drop signed URL as soon as transport no longer needs it
    m_active = false;
    emit ready(m_token);
}
void MediaBuffer::fail(PlaybackError error)
{
    if (!m_active)
        return;
    const auto token = m_token;
    cancel();
    emit failed(token, PlaybackFailure{error});
}
} // namespace cirrus::media
