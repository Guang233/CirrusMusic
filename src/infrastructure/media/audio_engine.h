#pragma once
#include "application/playback/observed_playback.h"
#include "application/ports/playback.h"
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

namespace cirrus::media
{
class AudioOutput;
class AudioStore;
class AudioStream;

// The playback engine (ADR-0008): AudioStore streams the file, FFmpeg decodes it on a
// producer thread into a ring, QAudioSink's callback plays it. Owner-thread object (the
// backend thread); events: Duration, Seekable, Volume
// and Ready once the file opens, State on play/pause, Position about every 100 ms,
// SeekConfirmed when a seek has taken effect, Observation, Ended, Failure. A preloaded
// load is decoded straight after the current one (gapless) and becomes current with
// Advanced, then its Duration and Seekable; SourceExpired asks for a fresh address.
class AudioEngine : public application::IPlaybackBackend
{
    Q_OBJECT
  public:
    explicit AudioEngine(AudioStore& store, QObject* parent = nullptr);
    ~AudioEngine() override;
    void load(domain::MediaSource source, domain::LoadToken token) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;
    void setVolume(float linear) override;
    void preload(domain::MediaSource source, domain::LoadToken next) override;
    void refresh(domain::LoadToken token, domain::MediaSource source) override;

  private:
    struct Producer;
    struct Next {
        domain::MediaSource source;
        domain::LoadToken token;
        std::shared_ptr<AudioStream> stream; // opened shortly before it is needed
    };
    void advance(qint64 durationMs, quint64 boundary);
    void openNext();
    void dropNext();
    void urlNeeded(quint64 stream);
    [[nodiscard]] qint64 playedFrames() const;
    void opened(domain::LoadToken token, qint64 durationMs);
    void tick();
    void fail(domain::LoadToken token, domain::PlaybackError error);
    void ended();
    void send(application::PlaybackEvent event);
    void sendLater(application::PlaybackEvent event);
    void deviceChanged();
    void startProducer(qint64 positionMs, bool seek = false);
    void stopProducer();
    [[nodiscard]] qint64 position() const;
    [[nodiscard]] qint64 clipLength() const;

    AudioStore& m_store;
    std::unique_ptr<AudioOutput> m_output;
    std::shared_ptr<Producer> m_producer;
    std::shared_ptr<AudioStream> m_stream;
    domain::MediaSource m_source;
    domain::LoadToken m_token;
    std::optional<Next> m_next;
    QTimer m_ticker;
    QTimer m_openDeadline;
    QTimer m_idle; // suspends the device a moment after a pause
    QElapsedTimer m_clock;
    application::ObservedPlaybackMeter m_meter;
    std::optional<domain::ObservedPlayback> m_published;
    qint64 m_durationMs = 0;
    qint64 m_lastPosition = -1;
    quint64 m_seekSerial = 0;
    quint64 m_underruns = 0;
    float m_volume = 0.5f;
    bool m_active = false;
    bool m_ready = false;
    bool m_playing = false;
    bool m_ended = false;
};
} // namespace cirrus::media
