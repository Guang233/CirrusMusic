#include "cover_provider.h"

#include "sources.h"

#include <QBuffer>
#include <QImageReader>
#include <QThreadPool>
#include <QUrl>

namespace cirrus::ui
{

namespace
{
class CoverResponse;

// One cover request, shared by the response (image loader thread), the source request
// (GUI thread) and the decode job (thread pool).
struct Job {
    QMutex mutex;
    CoverResponse* response = nullptr; // null once canceled or destroyed
    Request request;                   // GUI thread only
};

class CoverResponse : public QQuickImageResponse
{
  public:
    explicit CoverResponse(std::shared_ptr<Job> job) : m_job(std::move(job))
    {
        const QMutexLocker lock(&m_job->mutex);
        m_job->response = this;
    }
    ~CoverResponse() override { detach(); }

    QQuickTextureFactory* textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
    QString errorString() const override { return m_error; }

    // Results of a canceled request are still cached; only the delivery is dropped.
    void cancel() override
    {
        detach();
        emit finished();
    }

    // Any thread, with the job mutex held.
    void finish(const QImage& image, const QString& error)
    {
        m_image = image;
        m_error = error;
        emit finished();
    }

  private:
    void detach()
    {
        const QMutexLocker lock(&m_job->mutex);
        m_job->response = nullptr;
    }

    std::shared_ptr<Job> m_job;
    QImage m_image;
    QString m_error;
};

void deliver(const std::shared_ptr<Job>& job, const QImage& image, const QString& error)
{
    const QMutexLocker lock(&job->mutex);
    if (job->response)
        job->response->finish(image, error);
}

// Decodes at the requested size: covering it (the Image crops or fits), keeping the
// aspect ratio, never upscaling. A 0 dimension follows the other one, as with
// Image.sourceSize.
QImage decode(const QByteArray& bytes, const QSize& requested)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid() && (requested.width() > 0 || requested.height() > 0)) {
        const QSize bound(requested.width() > 0 ? requested.width() : original.width() * 1000,
                          requested.height() > 0 ? requested.height() : original.height() * 1000);
        const QSize target = original.scaled(bound, requested.width() > 0 && requested.height() > 0
                                                        ? Qt::KeepAspectRatioByExpanding
                                                        : Qt::KeepAspectRatio);
        if (target.width() < original.width())
            reader.setScaledSize(target);
    }
    return reader.read();
}
} // namespace

CoverProvider::CoverProvider(ImageSource* source) : m_source(source), m_guiContext(new QObject) {}

CoverProvider::~CoverProvider() { delete m_guiContext; }

QQuickImageResponse* CoverProvider::requestImageResponse(const QString& id,
                                                         const QSize& requestedSize)
{
    auto job = std::make_shared<Job>();
    auto* response = new CoverResponse(job);
    const QString key = QUrl::fromPercentEncoding(id.toUtf8());
    const QString cacheKey =
        QStringLiteral("%1@%2x%3").arg(key).arg(requestedSize.width()).arg(requestedSize.height());

    // Replies always come after this function returned (event loop or another thread).
    QImage cached;
    {
        const QMutexLocker lock(&m_cache->mutex);
        if (const QImage* hit = m_cache->images.object(cacheKey))
            cached = *hit;
    }
    // A hit is delivered from the GUI thread's event loop, never right away from
    // another thread: a caller on the GUI thread (Theme) connects to `finished` after
    // this returns.
    if (!cached.isNull()) {
        QMetaObject::invokeMethod(
            m_guiContext, [job, cached]() { deliver(job, cached, {}); }, Qt::QueuedConnection);
        return response;
    }

    QMetaObject::invokeMethod(
        m_guiContext,
        [job, key, cacheKey, requestedSize, cache = m_cache, source = m_source]() {
            {
                const QMutexLocker lock(&job->mutex);
                if (!job->response)
                    return;
            }
            job->request = source->image(
                key, qMax(requestedSize.width(), requestedSize.height()),
                [job, cacheKey, requestedSize, cache](Outcome<QByteArray> outcome) {
                    job->request.release();
                    if (!outcome.value) {
                        deliver(job, {}, QStringLiteral("cover %1 is unavailable").arg(cacheKey));
                        return;
                    }
                    QThreadPool::globalInstance()->start(
                        [job, cacheKey, requestedSize, cache, bytes = *outcome.value]() {
                            const QImage image = decode(bytes, requestedSize);
                            if (image.isNull()) {
                                deliver(job, {},
                                        QStringLiteral("cannot decode cover %1").arg(cacheKey));
                                return;
                            }
                            {
                                const QMutexLocker lock(&cache->mutex);
                                cache->images.insert(
                                    cacheKey, new QImage(image),
                                    int(qMax<qsizetype>(1, image.sizeInBytes() / 1024)));
                            }
                            deliver(job, image, {});
                        });
                });
        },
        Qt::QueuedConnection);
    return response;
}

} // namespace cirrus::ui
