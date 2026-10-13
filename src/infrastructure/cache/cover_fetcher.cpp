#include "cover_fetcher.h"
#include "infrastructure/network/cache_retention_policy.h"
#include "resource_cache_codec.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QImageReader>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QTimer>
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
};
CoverFetcher::CoverFetcher(QObject* parent) : QObject(parent)
{
    connect(&m_network, &QNetworkAccessManager::authenticationRequired, this,
            [](QNetworkReply* reply, QAuthenticator*) { reply->abort(); });
}
CoverFetcher::~CoverFetcher() { close(); }
QByteArray CoverFetcher::normalize(QByteArray bytes, int size)
{
    if (!validCoverSize(size) || bytes.isEmpty() || bytes.size() > 4 * 1024 * 1024)
        return {};
    QBuffer input(&bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto format = reader.format();
    const auto dimensions = reader.size();
    if ((format != "png" && format != "jpeg") || dimensions.isEmpty() ||
        dimensions.width() > 2048 || dimensions.height() > 2048 ||
        qint64(dimensions.width()) * dimensions.height() > 4 * 1024 * 1024 ||
        reader.supportsAnimation() || reader.imageCount() > 1)
        return {};
    const auto decoded = reader.read();
    if (decoded.isNull() || decoded.size() != dimensions)
        return {};
    const auto target = dimensions.scaled(size, size, Qt::KeepAspectRatio);
    QImage clean(target, QImage::Format_ARGB32_Premultiplied);
    clean.fill(Qt::transparent);
    {
        QPainter painter(&clean);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(clean.rect(), decoded);
    }
    QByteArray output;
    QBuffer buffer(&output);
    buffer.open(QIODevice::WriteOnly);
    if (!clean.save(&buffer, "PNG") || output.size() > maximumCachePayload(CacheKind::Cover))
        return {};
    return output;
}
void CoverFetcher::request(CacheJob job)
{
    if (m_closed || m_pending.size() >= 2 || m_pending.contains(job.id) ||
        !validCoverUrl(job.coverUrl) || !validCoverSize(job.coverSize)) {
        emit completed(job.id, CacheError::Busy, {}, 0);
        return;
    }
    auto pending = std::make_shared<Pending>();
    pending->job = std::move(job);
    const auto id = pending->job.id;
    pending->elapsed.start();
    QNetworkRequest request(pending->job.coverUrl);
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
    QByteArray png;
    qint64 ttl = 0;
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
            if (reply->error() != QNetworkReply::NoError ||
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
                (type != "image/jpeg" && type != "image/png"))
                error = CacheError::Network;
            else {
                png = normalize(std::move(pending->bytes), pending->job.coverSize);
                if (png.isEmpty())
                    error = CacheError::Unsupported;
                else if (pending->elapsed.elapsed() >= 15000) {
                    png.clear();
                    error = CacheError::Timeout;
                } else
                    ttl = retention(*reply, pending->elapsed.elapsed());
            }
        }
        if (!reply->isFinished())
            reply->abort();
        reply->deleteLater();
    } else
        error = CacheError::Network;
    emit completed(id, error, std::move(png), ttl);
}
void CoverFetcher::cancel(quint64 id) { finish(id, CacheError::Canceled); }
void CoverFetcher::close()
{
    m_closed = true;
    const auto ids = m_pending.keys();
    const QPointer<CoverFetcher> guard(this);
    for (const auto id : ids) {
        cancel(id);
        if (!guard)
            return;
    }
}
} // namespace cirrus::cache
