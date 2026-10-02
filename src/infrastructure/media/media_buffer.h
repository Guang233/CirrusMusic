#pragma once
#include "domain/playback/playback_types.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTemporaryFile>
#include <QTimer>
#include <memory>

namespace cirrus::media
{
// One bounded, nonpersistent buffer. No account cookies, auth headers or proxy credentials.
// The device belongs to this object and remains valid until cancel()/next start()/destruction.
class MediaBuffer final : public QObject
{
    Q_OBJECT
  public:
    explicit MediaBuffer(std::unique_ptr<QNetworkAccessManager> manager = {},
                         QObject* parent = nullptr);
    ~MediaBuffer() override;
    void start(domain::StreamSource source, domain::LoadToken token);
    void cancel();
    [[nodiscard]] QIODevice* device() const { return m_file.get(); }
    static constexpr qint64 MaximumBytes = 64LL * 1024 * 1024;
  signals:
    void ready(cirrus::domain::LoadToken token);
    void failed(cirrus::domain::LoadToken token, cirrus::domain::PlaybackFailure failure);

  private:
    void request(QUrl url);
    void consume();
    void finish();
    void fail(domain::PlaybackError error);
    void retireReply();
    std::unique_ptr<QNetworkAccessManager> m_manager;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QTemporaryFile> m_file;
    QTimer m_deadline;
    domain::StreamSource m_source;
    domain::LoadToken m_token;
    int m_redirects = 0;
    qint64 m_bytes = 0;
    bool m_active = false;
};
} // namespace cirrus::media
