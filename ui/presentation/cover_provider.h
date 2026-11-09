#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickAsyncImageProvider>

#include <memory>

namespace cirrus::ui
{

class ImageSource;

// `image://cover/<key>` for QML; size it with Image.sourceSize (docs/ui/presentation.md
// §4.3). Fetches the encoded image from the ImageSource on the GUI thread, decodes and
// scales it on a worker thread, and keeps decoded images in a bounded memory cache.
class CoverProvider : public QQuickAsyncImageProvider
{
  public:
    // `source` must outlive the provider (both belong to App).
    explicit CoverProvider(ImageSource* source);
    ~CoverProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

    // Shared with the decode jobs, which can outlive a canceled response.
    struct Cache {
        QMutex mutex;
        QCache<QString, QImage> images{128 * 1024}; // cost in KiB
    };

  private:
    ImageSource* m_source;
    std::shared_ptr<QObject> m_guiContext; // lives on the GUI thread; source calls go to it
    std::shared_ptr<Cache> m_cache = std::make_shared<Cache>();
};

} // namespace cirrus::ui
