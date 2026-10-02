#pragma once
#include "application/ports/playback.h"
#include "media_buffer.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <memory>

namespace cirrus::media
{
class QtPlaybackBackend final : public application::IPlaybackBackend
{
    Q_OBJECT
  public:
    explicit QtPlaybackBackend(std::unique_ptr<QNetworkAccessManager> manager = {},
                               QObject* parent = nullptr);
    ~QtPlaybackBackend() override;
    void load(domain::StreamSource source, domain::LoadToken token) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;
    void setVolume(float linear) override;

  private:
    void openBuffered(domain::LoadToken token);
    void fail(domain::LoadToken token, domain::PlaybackError error);
    void ended(domain::LoadToken token);
    void send(application::PlaybackEvent event);
    MediaBuffer m_buffer;
    QTimer m_decodeDeadline;
    std::unique_ptr<QAudioOutput> m_audio;
    std::unique_ptr<QMediaPlayer> m_player;
    domain::LoadToken m_token;
    QString m_format;
    std::optional<domain::PreviewRange> m_preview;
    bool m_active = false;
    bool m_ready = false;
    bool m_ended = false;
    float m_volume = 0.5f;
};
} // namespace cirrus::media
