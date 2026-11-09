#include "cover_fetcher.h"
#include "infrastructure/network/cache_retention_policy.h"
#include "logging/log.h"
#include "resource_cache_codec.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QImageReader>
#include <QNetworkReply>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace cirrus::cache
{
using namespace domain;
namespace
{
qint64 retention(const QNetworkReply& reply, qint64 elapsed)
{
    return network::cacheRetention(reply, elapsed).remainingMs();
}
} // namespace
struct CoverFetcher::Pending {
    CacheJob job;
    QPointer<QNetworkReply> reply;
    QByteArray bytes;
    QTimer* timer = nullptr;
    QElapsedTimer elapsed;
    qint64 ttlMs = 0;
};
CoverFetcher::CoverFetcher(QObject* parent) : QObject(parent)
{
    connect(&m_network, &QNetworkAccessManager::authenticationRequired, this,
            [](QNetworkReply* reply, QAuthenticator*) { reply->abort(); });
}
CoverFetcher::~CoverFetcher() { close(); }
namespace
{
// A reader on the image's header; `format` and `size` are known without decoding.
struct Header {
    QBuffer buffer;
    QImageReader reader;
    explicit Header(const QByteArray& bytes) : buffer()
    {
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        reader.setDevice(&buffer);
        reader.setDecideFormatFromContent(true);
        reader.setAutoTransform(true);
    }
    // The checks that need no decoding; nullptr when they pass.
    const char* rejection()
    {
        const auto format = reader.format();
        const auto dimensions = reader.size();
        if (format != "png" && format != "jpeg" && format != "webp")
            return "format";
        if (dimensions.isEmpty() || dimensions.width() > 2048 || dimensions.height() > 2048 ||
            qint64(dimensions.width()) * dimensions.height() > 4 * 1024 * 1024)
            return "dimensions";
        if (reader.supportsAnimation() || reader.imageCount() > 1)
            return "frames";
        return nullptr;
    }
};
} // namespace
const char* CoverFetcher::rejection(const QByteArray& bytes)
{
    if (bytes.isEmpty() || bytes.size() > maximumCachePayload(CacheKind::Cover))
        return "bytes";
    Header header(bytes);
    if (const char* why = header.rejection())
        return why;
    const auto dimensions = header.reader.size();
    const auto decoded = header.reader.read();
    if (decoded.isNull() || decoded.size() != dimensions)
        return "decode";
    return nullptr;
}
bool CoverFetcher::plausible(const QByteArray& bytes)
{
    if (bytes.isEmpty() || bytes.size() > maximumCachePayload(CacheKind::Cover))
        return false;
    Header header(bytes);
    return header.rejection() == nullptr;
}
void CoverFetcher::request(CacheJob job)
{
    // A few at once: the image hosts speak HTTP/2, these share one connection.
    if (m_closed || m_pending.size() + m_checking.size() >= 6 || m_pending.contains(job.id) ||
        m_checking.contains(job.id) || !validCoverUrl(job.coverUrl) ||
        !validCoverSize(job.coverSize)) {
        emit completed(job.id, CacheError::Busy, {}, 0);
        return;
    }
    auto pending = std::make_shared<Pending>();
    pending->job = std::move(job);
    const auto id = pending->job.id;
    pending->elapsed.start();
    // The image hosts scale on request: ask for the kept size rather than the original
    // (artist portraits run past normalize()'s 2048 px / 4 MiB limits). Only the request
    // carries the query; the cache key stays the bare URL.
    QUrl url = pending->job.coverUrl;
    url.setQuery(QStringLiteral("param=%1y%1").arg(pending->job.coverSize));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("image/png,image/jpeg"));
    request.setRawHeader(QByteArrayLiteral("Accept-Encoding"), QByteArrayLiteral("identity"));
    request.setTransferTimeout(5000);
    pending->reply = m_network.get(request);
    pending->reply->setReadBufferSize(64 * 1024);
    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    pending->timer->setInterval(15000);
    m_pending.insert(id, pending);
    connect(pending->timer, &QTimer::timeout, this,
            [this, id] { finish(id, CacheError::Timeout); });
    connect(pending->reply, &QIODevice::readyRead, this, [this, id] { consume(id); });
    connect(pending->reply, &QNetworkReply::metaDataChanged, this, [this, id] { consume(id); });
    connect(pending->reply, &QNetworkReply::finished, this, [this, id] {
        const QPointer<CoverFetcher> guard(this);
        consume(id);
        if (guard && m_pending.contains(id))
            finish(id, CacheError::None);
    });
    pending->timer->start();
}
void CoverFetcher::consume(quint64 id)
{
    const auto pending = m_pending.value(id);
    if (!pending || !pending->reply)
        return;
    auto* reply = pending->reply.data();
    if (pending->elapsed.elapsed() >= 15000) {
        finish(id, CacheError::Timeout);
        return;
    }
    const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qsizetype headers = 0;
    const auto pairs = reply->rawHeaderPairs();
    for (const auto& pair : pairs)
        headers += pair.first.size() + pair.second.size();
    const auto encoding =
        reply->rawHeader(QByteArrayLiteral("Content-Encoding")).trimmed().toLower();
    if (pairs.size() > 64 || headers > 16384 || (code && code != 200) ||
        (!encoding.isEmpty() && encoding != "identity")) {
        finish(id, CacheError::Network);
        return;
    }
    const auto declared = reply->header(QNetworkRequest::ContentLengthHeader);
    if (declared.isValid() &&
        (declared.toLongLong() < 0 || declared.toLongLong() > 4 * 1024 * 1024)) {
        finish(id, CacheError::Capacity);
        return;
    }
    while (reply->bytesAvailable() > 0) {
        const auto remaining = 4 * 1024 * 1024 - pending->bytes.size();
        if (!remaining) {
            finish(id, CacheError::Capacity);
            return;
        }
        const auto chunk = reply->read(std::min<qint64>(remaining, 64 * 1024));
        if (chunk.isEmpty())
            break;
        pending->bytes += chunk;
    }
}
void CoverFetcher::finish(quint64 id, CacheError error)
{
    const auto pending = m_pending.take(id);
    if (!pending)
        return;
    pending->timer->stop();
    pending->timer->deleteLater();
    if (error == CacheError::None && pending->elapsed.elapsed() >= 15000)
        error = CacheError::Timeout;
    if (pending->reply) {
        auto* reply = pending->reply.data();
        reply->disconnect(this);
        if (error == CacheError::None) {
            const auto type = reply->rawHeader(QByteArrayLiteral("Content-Type"))
                                  .split(';')
                                  .value(0)
                                  .trimmed()
                                  .toLower();
            // The image hosts send JPEG covers as the nonstandard "image/jpg", and WebP
            // uploads under that label too; the bytes' real format is checked by
            // rejection() (WebP only when Qt's WebP plugin is there).
            if (reply->error() != QNetworkReply::NoError ||
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
                (type != "image/jpeg" && type != "image/jpg" && type != "image/png" &&
                 type != "image/webp"))
                error = CacheError::Network;
            else
                pending->ttlMs = retention(*reply, pending->elapsed.elapsed());
        }
        if (!reply->isFinished())
            reply->abort();
        reply->deleteLater();
    } else
        error = CacheError::Network;
    if (error != CacheError::None) {
        complete(pending, error, {}, 0);
        return;
    }
    // Decoding takes a while: off this thread. The context lives on this thread for as
    // long as the pool job may post back (deleted later, from whichever thread is last).
    m_checking.insert(id, pending);
    const std::shared_ptr<QObject> context(new QObject, [](QObject* o) { o->deleteLater(); });
    context->moveToThread(thread());
    QThreadPool::globalInstance()->start(
        [context, self = QPointer<CoverFetcher>(this), id, bytes = pending->bytes]() mutable {
            const char* rejected = rejection(bytes);
            // `self` is only read back on this object's thread.
            QMetaObject::invokeMethod(
                context.get(),
                [self = std::move(self), id, rejected]() {
                    if (self)
                        self->checked(id, rejected);
                },
                Qt::QueuedConnection);
        });
}
void CoverFetcher::checked(quint64 id, const char* rejected)
{
    const auto pending = m_checking.take(id);
    if (!pending)
        return; // canceled meanwhile
    if (rejected) {
        qCDebug(lcCache) << log::event("cover.rejected") << log::field("id", id)
                         << log::field("reason", rejected);
        complete(pending, CacheError::Unsupported, {}, 0);
        return;
    }
    auto bytes = std::move(pending->bytes);
    complete(pending, CacheError::None, std::move(bytes), pending->ttlMs);
}
void CoverFetcher::complete(const std::shared_ptr<Pending>& pending, CacheError error,
                            QByteArray bytes, qint64 ttlMs)
{
    const auto id = pending->job.id;
    if (error == CacheError::None)
        qCDebug(lcCache) << log::event("cover.fetched") << log::field("id", id)
                         << log::field("bytes", bytes.size())
                         << log::field("ms", pending->elapsed.elapsed());
    else if (error != CacheError::Canceled)
        qCWarning(lcCache) << log::event("cover.failed") << log::field("id", id)
                           << log::field("err", error)
                           << log::field("host", log::host(pending->job.coverUrl))
                           << log::field("ms", pending->elapsed.elapsed());
    emit completed(id, error, std::move(bytes), ttlMs);
}
void CoverFetcher::cancel(quint64 id)
{
    if (const auto pending = m_checking.take(id)) {
        complete(pending, CacheError::Canceled, {}, 0);
        return;
    }
    finish(id, CacheError::Canceled);
}
void CoverFetcher::close()
{
    m_closed = true;
    QList<quint64> ids = m_pending.keys();
    ids << m_checking.keys();
    const QPointer<CoverFetcher> guard(this);
    for (const auto id : ids) {
        cancel(id);
        if (!guard)
            return;
    }
}
} // namespace cirrus::cache
