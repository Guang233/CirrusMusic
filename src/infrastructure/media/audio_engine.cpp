#include "audio_engine.h"
#include "audio_decoder.h"
#include "audio_output.h"
#include "audio_store.h"
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cirrus::media
{
using namespace domain;
using namespace application;
using Kind = PlaybackEvent::Kind;
namespace
{
constexpr qsizetype kDecodeFrames = 2048;
constexpr int kTickMs = 100;
constexpr int kOpenTimeoutMs = 30000;
constexpr int kSuspendAfterMs = 300; // past the fade-out
} // namespace

// The decoder thread and what it shares with the owner.
struct AudioEngine::Producer {
    std::mutex lock;
    std::condition_variable wake;
    bool quit = false;
    std::optional<qint64> seekTo;
    qint64 baseMs = 0;                // the position output frame 0 stands for (lock)
    quint64 seekSerial = 0;           // bumped when a seek took effect (lock)
    std::atomic<quint64> produced{0}; // frames into the ring since the last flush
    std::atomic<bool> eof{false};
    AudioOutput* output = nullptr; // outlives the thread: the owner joins first
    int outRate = 0;
    std::shared_ptr<AudioStream> stream;
    QString format;
    std::thread thread;

    [[nodiscard]] bool quitting()
    {
        std::lock_guard guard(lock);
        return quit;
    }
    void run(AudioEngine* engine, LoadToken token, bool reopening);
};

void AudioEngine::Producer::run(AudioEngine* engine, LoadToken token, bool reopening)
{
    const auto failed = [&](PlaybackError error) {
        if (quitting())
            return; // stopping interrupted it; not a failure
        QMetaObject::invokeMethod(
            engine, [engine, token, error] { engine->fail(token, error); }, Qt::QueuedConnection);
    };
    AudioDecoder decoder;
    if (!decoder.open(stream, format)) {
        failed(decoder.error().value_or(PlaybackError::Decode));
        return;
    }
    if (!reopening) {
        const qint64 duration = decoder.durationMs();
        QMetaObject::invokeMethod(
            engine, [engine, token, duration] { engine->opened(token, duration); },
            Qt::QueuedConnection);
    }
    auto resampler = std::make_unique<AudioResampler>(decoder.sampleRate(), outRate);
    AudioRing& ring = *output->ring();
    std::vector<float> chunk, out;
    size_t written = 0;   // of `out`
    bool flushing = true; // the owner flushed before starting us
    while (true) {
        std::optional<qint64> seek;
        {
            std::lock_guard guard(lock);
            if (quit)
                return;
            seek = std::exchange(seekTo, std::nullopt);
        }
        if (seek) {
            if (!flushing)
                output->beginFlush();
            flushing = true;
            out.clear();
            written = 0;
            if (!decoder.seek(*seek)) {
                if (decoder.interrupted())
                    continue; // a newer seek or stop came in
                failed(decoder.error().value_or(PlaybackError::Decode));
                return;
            }
            resampler = std::make_unique<AudioResampler>(decoder.sampleRate(), outRate);
            produced = 0;
            eof = false;
            std::lock_guard guard(lock);
            baseMs = *seek;
            ++seekSerial;
            continue;
        }
        if (decoder.interrupted())
            decoder.resume(); // interrupted for a seek or stop that isn't (or no longer) here
        if (written < out.size()) {
            const auto frames =
                ring.write(out.data() + written, qsizetype(out.size() - written) / 2);
            written += size_t(frames) * 2;
            produced += quint64(frames);
            if (flushing) {
                output->endFlush(); // the first audio is in: the consumer may go on
                flushing = false;
            }
            if (written < out.size()) {
                std::unique_lock guard(lock);
                wake.wait_for(guard, std::chrono::milliseconds(5),
                              [this] { return quit || seekTo.has_value(); });
            }
            continue;
        }
        out.clear();
        written = 0;
        if (eof) {
            std::unique_lock guard(lock);
            wake.wait_for(guard, std::chrono::milliseconds(20),
                          [this] { return quit || seekTo.has_value(); });
            continue;
        }
        chunk.clear();
        if (decoder.read(chunk, kDecodeFrames)) {
            resampler->process(chunk, out);
            continue;
        }
        if (decoder.error()) {
            failed(*decoder.error());
            return;
        }
        if (decoder.interrupted())
            continue; // a seek or stop is waiting
        resampler->flush(out);
        eof = true;
        if (out.empty() && flushing) {
            output->endFlush();
            flushing = false;
        }
    }
}

AudioEngine::AudioEngine(AudioStore& store, QObject* parent)
    : IPlaybackBackend(parent), m_store(store), m_output(std::make_unique<AudioOutput>())
{
    m_ticker.setInterval(kTickMs);
    m_ticker.setTimerType(Qt::PreciseTimer);
    connect(&m_ticker, &QTimer::timeout, this, &AudioEngine::tick);
    m_openDeadline.setSingleShot(true);
    connect(&m_openDeadline, &QTimer::timeout, this, [this] {
        if (m_active && !m_ready)
            fail(m_token, PlaybackError::Timeout);
    });
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (!m_playing)
            m_output->suspend();
    });
    connect(m_output.get(), &AudioOutput::deviceChanged, this, &AudioEngine::deviceChanged);
}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::send(PlaybackEvent value)
{
    if (m_active && value.token == m_token)
        emit event(std::move(value));
}

void AudioEngine::sendLater(PlaybackEvent value)
{
    // Like the old backend's queued signals: never re-enter the caller of play()/pause().
    QMetaObject::invokeMethod(this, [this, value] { send(value); }, Qt::QueuedConnection);
}

void AudioEngine::load(MediaSource source, LoadToken token)
{
    Q_ASSERT(QThread::currentThread() == thread());
    stop();
    m_token = token;
    m_active = true;
    m_source = std::move(source);
    m_clock.start();
    PlaybackError error{};
    m_stream = m_store.open(m_source, &error);
    if (!m_stream) {
        fail(token, error);
        return;
    }
    if (!m_output->isOpen() && !m_output->open()) {
        fail(token, PlaybackError::NoOutput);
        return;
    }
    m_output->setGain(m_volume);
    m_output->setPlaying(false);
    m_openDeadline.start(kOpenTimeoutMs);
    startProducer(0);
}

void AudioEngine::startProducer(qint64 positionMs)
{
    auto producer = std::make_shared<Producer>();
    producer->output = m_output.get();
    producer->outRate = m_output->sampleRate();
    producer->stream = m_stream;
    producer->format = m_source.format.toLower();
    producer->baseMs = positionMs;
    if (positionMs > 0)
        producer->seekTo = positionMs;
    m_output->beginFlush();     // nothing old plays; the producer ends it with new audio
    m_stream->clearInterrupt(); // left over from stopping the previous producer
    const bool reopening = m_ready;
    Producer* raw = producer.get();
    producer->thread = std::thread(
        [raw, engine = this, token = m_token, reopening] { raw->run(engine, token, reopening); });
    m_producer = std::move(producer);
    m_seekSerial = 0;
}

void AudioEngine::stopProducer()
{
    if (!m_producer)
        return;
    {
        std::lock_guard guard(m_producer->lock);
        m_producer->quit = true;
    }
    m_producer->wake.notify_all();
    if (m_stream)
        m_stream->interrupt(); // a read waiting for the download returns
    if (m_producer->thread.joinable())
        m_producer->thread.join();
    m_producer.reset();
    m_output->beginFlush(); // whatever it queued never plays
}

void AudioEngine::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_active = false;
    m_ready = false;
    m_playing = false;
    m_ended = false;
    m_durationMs = 0;
    m_lastPosition = -1;
    m_ticker.stop();
    m_openDeadline.stop();
    m_meter.reset();
    m_published.reset();
    stopProducer();
    if (m_stream) {
        m_stream->cancel();
        m_stream.reset(); // releasing it stops its download; what came stays cached
    }
    m_output->setPlaying(false);
    m_idle.start(kSuspendAfterMs);
}

qint64 AudioEngine::clipLength() const
{
    if (m_source.preview)
        return std::min(m_durationMs, m_source.preview->endMs - m_source.preview->startMs);
    return m_durationMs;
}

qint64 AudioEngine::position() const
{
    if (!m_producer || m_output->sampleRate() <= 0)
        return 0;
    qint64 base;
    quint64 serial;
    std::optional<qint64> pending;
    {
        std::lock_guard guard(m_producer->lock);
        base = m_producer->baseMs;
        serial = m_producer->seekSerial;
        pending = m_producer->seekTo;
    }
    if (pending)
        return *pending; // the seek hasn't taken effect yet
    Q_UNUSED(serial);
    const qint64 played =
        std::max<qint64>(0, qint64(m_output->consumed()) - m_output->latencyFrames());
    return std::min(clipLength(), base + played * 1000 / m_output->sampleRate());
}

void AudioEngine::opened(LoadToken token, qint64 durationMs)
{
    if (!m_active || token != m_token || m_ready)
        return;
    m_openDeadline.stop();
    if (m_source.preview &&
        durationMs > m_source.preview->endMs - m_source.preview->startMs + 250) {
        fail(token, PlaybackError::InvalidResponse);
        return;
    }
    m_durationMs = durationMs;
    m_ready = true;
    m_ticker.start();
    send({token, Kind::Duration, {}, durationMs});
    if (!m_active || token != m_token)
        return;
    send({token, Kind::Seekable, {}, 1});
    if (!m_active || token != m_token)
        return;
    PlaybackEvent volume{token, Kind::Volume};
    volume.volumeLinear = m_volume;
    send(volume);
    if (!m_active || token != m_token)
        return;
    send({token, Kind::Ready});
}

void AudioEngine::tick()
{
    if (!m_active || !m_ready || m_ended || !m_producer)
        return;
    const auto token = m_token;
    qint64 base;
    quint64 serial;
    bool seeking;
    {
        std::lock_guard guard(m_producer->lock);
        base = m_producer->baseMs;
        serial = m_producer->seekSerial;
        seeking = m_producer->seekTo.has_value();
    }
    if (serial != m_seekSerial) {
        m_seekSerial = serial;
        m_meter.breakInterval();
        m_lastPosition = base;
        send({token, Kind::SeekConfirmed, {}, base});
        if (!m_active || token != m_token)
            return;
    }
    const qint64 now = position();
    const bool drained = m_producer->eof && m_output->consumed() >= m_producer->produced;
    if ((m_source.preview && now >= clipLength()) || drained) {
        ended();
        return;
    }
    if (!seeking && now != m_lastPosition) {
        m_lastPosition = now;
        send({token, Kind::Position, {}, now});
        if (!m_active || token != m_token)
            return;
    }
    const auto underruns = m_output->underruns();
    const bool stalled = underruns != m_underruns;
    m_underruns = underruns;
    const bool eligible = m_playing && !seeking && !stalled && m_volume > 0 && !m_output->silent();
    const auto progress = m_meter.sample(now, m_clock.elapsed(), eligible);
    if (m_published == progress)
        return;
    m_published = progress;
    PlaybackEvent observation{token, Kind::Observation};
    observation.observed = progress;
    send(observation); // last: observers may retire this load synchronously
}

void AudioEngine::ended()
{
    if (!m_active || m_ended)
        return;
    m_ended = true;
    m_ticker.stop();
    m_meter.breakInterval();
    m_playing = false;
    m_output->setPlaying(false);
    m_idle.start(kSuspendAfterMs);
    send({m_token, Kind::Ended});
}

void AudioEngine::fail(LoadToken token, PlaybackError error)
{
    if (!m_active || token != m_token)
        return;
    m_meter.breakInterval();
    m_ticker.stop();
    QTimer::singleShot(0, this, [this, token, error] {
        if (!m_active || token != m_token)
            return;
        stop();
        emit event({token, Kind::Failure, {}, 0, {error}});
    });
}

void AudioEngine::play()
{
    if (!m_active || !m_ready || m_ended)
        return;
    m_playing = true;
    m_idle.stop();
    m_meter.breakInterval();
    m_output->resume();
    m_output->setPlaying(true);
    sendLater({m_token, Kind::State, PlaybackState::Playing});
}

void AudioEngine::pause()
{
    m_meter.breakInterval();
    if (!m_active)
        return;
    m_playing = false;
    m_output->setPlaying(false);
    m_idle.start(kSuspendAfterMs);
    if (m_ready)
        sendLater({m_token, Kind::State, PlaybackState::Paused});
}

void AudioEngine::seek(qint64 positionMs)
{
    if (!m_active || !m_ready || m_ended || !m_producer)
        return;
    const auto target = std::clamp<qint64>(positionMs, 0, std::max<qint64>(0, clipLength() - 1));
    m_meter.breakInterval();
    {
        std::lock_guard guard(m_producer->lock);
        m_producer->seekTo = target;
    }
    m_producer->wake.notify_all();
    if (m_stream)
        m_stream->interrupt(); // don't wait out a download where we no longer play
}

void AudioEngine::setVolume(float linear)
{
    if (!std::isfinite(linear) || linear < 0 || linear > 1)
        return;
    if ((linear == 0) != (m_volume == 0))
        m_meter.breakInterval();
    m_volume = linear;
    m_output->setGain(linear);
    if (m_active && m_ready) {
        PlaybackEvent feedback{m_token, Kind::Volume};
        feedback.volumeLinear = linear;
        sendLater(feedback);
    }
}

void AudioEngine::deviceChanged()
{
    if (!m_active || !m_ready || m_ended) {
        if (m_output->isOpen())
            m_output->open(); // follow the default for the next load
        return;
    }
    const qint64 at = position();
    stopProducer();
    if (!m_output->open())
        return; // no device now; the next change (one appears) picks it up from here
    m_output->setGain(m_volume);
    m_output->setPlaying(m_playing);
    startProducer(std::max<qint64>(at, 1));
}
} // namespace cirrus::media
