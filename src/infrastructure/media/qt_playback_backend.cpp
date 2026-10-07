#include "qt_playback_backend.h"
#include <QAudioDevice>
#include <QMediaDevices>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace cirrus::media
{
using namespace domain;
using namespace application;
using Kind = PlaybackEvent::Kind;
QtPlaybackBackend::QtPlaybackBackend(std::unique_ptr<QNetworkAccessManager> manager,
                                     QObject* parent)
    : IPlaybackBackend(parent), m_buffer(std::move(manager))
{
    m_sampleTimer.setInterval(250);
    m_sampleTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sampleTimer, &QTimer::timeout, this, &QtPlaybackBackend::sampleProgress);
    m_decodeDeadline.setSingleShot(true);
    connect(&m_decodeDeadline, &QTimer::timeout, this, [this] {
        if (m_active && !m_ready)
            fail(m_token, PlaybackError::Timeout);
    });
    connect(&m_buffer, &MediaBuffer::ready, this, &QtPlaybackBackend::openBuffered);
    connect(&m_buffer, &MediaBuffer::failed, this,
            [this](LoadToken token, PlaybackFailure failure) { fail(token, failure.error); });
}
QtPlaybackBackend::~QtPlaybackBackend() { stop(); }
void QtPlaybackBackend::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_active = false;
    m_ready = false;
    m_decodeDeadline.stop();
    m_sampleTimer.stop();
    m_meter.reset();
    m_publishedProgress.reset();
    m_seekTarget.reset();
    if (m_player) {
        disconnect(m_player.get(), nullptr, this, nullptr);
        m_player->stop();
        m_player.reset(); // decoder must release the device BEFORE its buffer is removed
    }
    m_audio.reset();
    m_buffer.cancel();
    m_preview.reset();
}
void QtPlaybackBackend::load(MediaSource source, LoadToken token)
{
    stop();
    m_token = token;
    m_active = true;
    m_ended = false;
    m_format = source.format;
    m_preview = source.preview;
    m_sampleClock.start();
    m_buffer.start(std::move(source), token);
}
void QtPlaybackBackend::send(PlaybackEvent value)
{
    if (m_active && value.token == m_token)
        emit event(std::move(value));
}
void QtPlaybackBackend::sampleProgress()
{
    if (!m_active || !m_ready || m_ended || !m_player)
        return;
    const bool eligible = !m_seekTarget && m_player->isPlaying() &&
                          m_player->mediaStatus() == QMediaPlayer::BufferedMedia &&
                          m_player->playbackRate() == 1.0 && m_audio && !m_audio->isMuted() &&
                          m_audio->volume() > 0;
    auto position = m_player->position();
    if (m_player->duration() > 0)
        position = qMin(position, m_player->duration());
    if (m_preview)
        position = qMin(position, m_preview->endMs - m_preview->startMs);
    const auto progress = m_meter.sample(position, m_sampleClock.elapsed(), eligible);
    if (m_publishedProgress == progress)
        return;
    m_publishedProgress = progress;
    PlaybackEvent observation{m_token, Kind::Observation};
    observation.observed = progress;
    send(observation); // last operation: observers may retire this load synchronously
}
void QtPlaybackBackend::fail(LoadToken token, PlaybackError error)
{
    if (!m_active || token != m_token)
        return;
    m_meter.breakInterval();
    m_sampleTimer.stop();
    // Deferring disposal avoids destroying a QMediaPlayer within its own signal emission.
    QTimer::singleShot(0, this, [this, token, error] {
        if (!m_active || token != m_token)
            return;
        stop();
        emit event({token, Kind::Failure, {}, 0, {error}});
    });
}
void QtPlaybackBackend::openBuffered(LoadToken token)
{
    if (!m_active || token != m_token)
        return;
    const auto head = m_buffer.device()->peek(12);
    const bool mp3 = head.startsWith("ID3") ||
                     (head.size() >= 2 && static_cast<unsigned char>(head[0]) == 0xff &&
                      (static_cast<unsigned char>(head[1]) & 0xe0) == 0xe0);
    const bool matches =
        (m_format == QStringLiteral("mp3") && mp3) ||
        (m_format == QStringLiteral("flac") && head.startsWith("fLaC")) ||
        (m_format == QStringLiteral("wav") && head.startsWith("RIFF") && head.mid(8, 4) == "WAVE");
    if (!matches) {
        fail(token, PlaybackError::UnsupportedFormat);
        return;
    }
    const auto output = QMediaDevices::defaultAudioOutput();
    if (output.isNull()) {
        fail(token, PlaybackError::NoOutput);
        return;
    }
    m_audio = std::make_unique<QAudioOutput>(output);
    m_audio->setVolume(m_volume);
    m_audio->setMuted(m_volume == 0);
    m_player =
        std::make_unique<QMediaPlayer>(); // one object/context per load, NEVER re-tag old signals
    m_player->setAudioOutput(m_audio.get());
    // Break immediately at state/status transitions, including brief stalls between timer ticks.
    // These callbacks do not emit outward signals or destroy the player in its signal stack.
    connect(m_player.get(), &QMediaPlayer::mediaStatusChanged, this, [this, token] {
        if (m_active && token == m_token)
            m_meter.breakInterval();
    });
    connect(m_player.get(), &QMediaPlayer::playbackStateChanged, this, [this, token] {
        if (m_active && token == m_token)
            m_meter.breakInterval();
    });
    connect(m_player.get(), &QMediaPlayer::playbackRateChanged, this, [this, token] {
        if (m_active && token == m_token)
            m_meter.breakInterval();
    });
    connect(m_player.get(), &QMediaPlayer::positionChanged, this, [this, token](qint64 position) {
        if (m_active && token == m_token && m_seekTarget && position == *m_seekTarget) {
            m_seekTarget.reset();
            m_meter.breakInterval();
        }
    });
    connect(
        m_player.get(), &QMediaPlayer::errorOccurred, this,
        [this, token](QMediaPlayer::Error, const QString&) { fail(token, PlaybackError::Decode); },
        Qt::AutoConnection);
    connect(
        m_player.get(), &QMediaPlayer::mediaStatusChanged, this,
        [this, token](QMediaPlayer::MediaStatus status) {
            if (!m_active || token != m_token)
                return;
            if (status == QMediaPlayer::LoadedMedia && !m_ready) {
                if (m_preview &&
                    m_player->duration() > m_preview->endMs - m_preview->startMs + 250) {
                    fail(token, PlaybackError::InvalidResponse);
                    return;
                }
                m_ready = true;
                m_decodeDeadline.stop();
                m_sampleTimer.start();
                send({token, Kind::Duration, {}, m_player->duration()});
                if (!m_active || token != m_token)
                    return;
                send({token, Kind::Seekable, {}, m_player->isSeekable()});
                if (!m_active || token != m_token)
                    return;
                send({token, Kind::Ready});
            } else if (status == QMediaPlayer::EndOfMedia)
                ended(token);
        },
        Qt::QueuedConnection);
    connect(
        m_player.get(), &QMediaPlayer::playbackStateChanged, this,
        [this, token](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::PlayingState)
                send({token, Kind::State, PlaybackState::Playing});
            else if (state == QMediaPlayer::PausedState)
                send({token, Kind::State, PlaybackState::Paused});
        },
        Qt::QueuedConnection);
    connect(
        m_player.get(), &QMediaPlayer::positionChanged, this,
        [this, token](qint64 value) {
            if (!m_active || token != m_token)
                return;
            if (m_preview && value >= m_preview->endMs - m_preview->startMs) {
                ended(token);
                return;
            }
            send({token, Kind::Position, {}, value});
        },
        Qt::QueuedConnection);
    connect(
        m_player.get(), &QMediaPlayer::seekableChanged, this,
        [this, token](bool value) { send({token, Kind::Seekable, {}, value}); },
        Qt::QueuedConnection);
    // Custom seekable device: Qt/FFmpeg receives neither the signed URL nor credentials.
    // Only standalone MP3/FLAC (WAV for development fixtures), not playlists or external
    // references.
    m_decodeDeadline.start(15000);
    m_player->setSourceDevice(m_buffer.device());
}
void QtPlaybackBackend::ended(LoadToken token)
{
    if (!m_active || token != m_token || m_ended)
        return;
    m_ended = true;
    m_sampleTimer.stop();
    m_meter.breakInterval();
    if (m_player)
        m_player->pause();
    send({token, Kind::Ended});
}
void QtPlaybackBackend::play()
{
    if (m_ready && m_player && !m_ended)
        m_player->play();
}
void QtPlaybackBackend::pause()
{
    m_meter.breakInterval();
    if (m_player)
        m_player->pause();
}
void QtPlaybackBackend::seek(qint64 positionMs)
{
    if (!m_ready || !m_player || !m_player->isSeekable())
        return;
    auto maximum = m_player->duration();
    if (m_preview)
        maximum = qMin(maximum, m_preview->endMs - m_preview->startMs);
    const auto target = std::clamp<qint64>(positionMs, 0, qMax<qint64>(0, maximum - 1));
    m_meter.breakInterval();
    if (m_player->position() == target && !m_seekTarget)
        return; // no asynchronous jump needed
    m_seekTarget = target;
    m_player->setPosition(target);
}
void QtPlaybackBackend::setVolume(float linear)
{
    if (!std::isfinite(linear) || linear < 0 || linear > 1)
        return;
    if ((linear == 0) != (m_volume == 0))
        m_meter.breakInterval();
    m_volume = linear;
    if (m_audio) {
        m_audio->setVolume(linear);
        m_audio->setMuted(linear == 0);
    }
}
} // namespace cirrus::media
