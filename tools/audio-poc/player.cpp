#include "player.h"

#include <QAudioSink>
#include <QMediaDevices>

#include <cstdio>
#include <cstring>

namespace poc
{
namespace
{
constexpr qsizetype kProduceFrames = 2048;
constexpr double kRingSeconds = 0.5;
constexpr double kRampSeconds = 0.008; // pause/seek fade
} // namespace

void Ring::write(const float* in, qsizetype frames)
{
    const quint64 at = m_write.load(std::memory_order_relaxed);
    for (qsizetype i = 0; i < frames; ++i) {
        const size_t slot = size_t((at + quint64(i)) % quint64(m_frames)) * 2;
        m_data[slot] = in[i * 2];
        m_data[slot + 1] = in[i * 2 + 1];
    }
    m_write.store(at + quint64(frames), std::memory_order_release);
}

qsizetype Ring::read(float* out, qsizetype frames)
{
    const quint64 at = m_read.load(std::memory_order_relaxed);
    const qsizetype take = std::min(frames, readable());
    for (qsizetype i = 0; i < take; ++i) {
        const size_t slot = size_t((at + quint64(i)) % quint64(m_frames)) * 2;
        out[i * 2] = m_data[slot];
        out[i * 2 + 1] = m_data[slot + 1];
    }
    m_read.store(at + quint64(take), std::memory_order_release);
    return take;
}

Player::Player(QStringList tracks, QObject* parent) : QObject(parent), m_tracks(std::move(tracks))
{
    connect(new QMediaDevices(this), &QMediaDevices::audioOutputsChanged, this,
            &Player::devicesChanged);
}

Player::~Player()
{
    closeSink();
    {
        std::lock_guard lock(m_lock);
        m_quit = true;
    }
    m_wake.notify_all();
    if (m_producer) {
        m_producer->wait();
        delete m_producer;
    }
}

bool Player::start()
{
    m_producer = QThread::create([this] { produce(); });
    m_producer->start();
    openSink(QMediaDevices::defaultAudioOutput(), 0, 0);
    return m_sink != nullptr;
}

void Player::openSink(const QAudioDevice& device, int track, qint64 ms)
{
    if (device.isNull()) {
        std::printf("no output device\n");
        return;
    }
    QAudioFormat format;
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    format.setSampleRate(device.preferredFormat().sampleRate());
    if (!device.isFormatSupported(format))
        format.setSampleRate(48000);
    m_device = device;
    if (format.sampleRate() != m_rate || !m_ring) {
        std::lock_guard lock(m_lock);
        m_rate = format.sampleRate();
        m_ring = std::make_unique<Ring>(qsizetype(m_rate * kRingSeconds));
    }
    // A new device (maybe another rate) restarts decoding where playback was.
    restart(track, ms);
    m_sink = std::make_unique<QAudioSink>(device, format);
    m_sink->setBufferFrameCount(m_rate / 50); // 20 ms
    m_sink->start([this](QSpan<float> out) { consume(out); });
    std::printf("output: %s, %d Hz, sink buffer %lld frames\n", qPrintable(device.description()),
                m_rate, static_cast<long long>(m_sink->bufferFrameCount()));
}

void Player::closeSink()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
}

void Player::restart(int track, qint64 ms)
{
    {
        std::lock_guard lock(m_lock);
        m_restart = true;
        m_restartTrack = track;
        m_restartMs = ms;
    }
    m_wake.notify_all();
}

void Player::produce()
{
    std::vector<float> chunk;
    while (true) {
        std::unique_lock lock(m_lock);
        if (m_quit)
            return;
        if (m_restart) {
            m_restart = false;
            // The consumer stops reading (silence) and says so; then the ring is ours to
            // empty. Without a running sink nobody answers: go on after a moment.
            m_flushSeen.store(false);
            m_flush.store(true);
            lock.unlock();
            for (int i = 0; i < 200 && !m_flushSeen.load(); ++i)
                QThread::usleep(500);
            lock.lock();
            m_ring->drop();
            m_chain = std::make_unique<Chain>(m_rate);
            m_chain->setTracks(m_tracks, m_restartTrack);
            if (m_restartMs > 0)
                m_chain->seek(m_restartTrack, m_restartMs);
            m_baseTrack = m_restartTrack;
            m_baseMs = m_restartMs;
            m_consumed.store(0);
            m_boundaries = m_chain->boundaries();
            // Some audio first, so the consumer doesn't fade in on an empty ring.
            chunk.clear();
            const qsizetype made = m_chain->pull(chunk, kProduceFrames * 4);
            m_ring->write(chunk.data(), made);
            m_flush.store(false, std::memory_order_release);
        }
        if (!m_chain || m_ring->writable() < kProduceFrames) {
            m_wake.wait_for(lock, std::chrono::milliseconds(5));
            continue;
        }
        chunk.clear();
        const qsizetype made = m_chain->pull(chunk, kProduceFrames);
        m_boundaries = m_chain->boundaries();
        lock.unlock();
        if (made == 0) {
            QThread::msleep(5);
            continue;
        }
        m_ring->write(chunk.data(), made);
    }
}

void Player::consume(QSpan<float> out)
{
    const qsizetype frames = out.size() / 2;
    float* data = out.data();
    if (m_flush.load(std::memory_order_acquire)) {
        m_flushSeen.store(true, std::memory_order_release);
        m_gain = 0;
        std::memset(data, 0, size_t(out.size()) * sizeof(float));
        return;
    }
    const bool paused = m_paused.load(std::memory_order_relaxed);
    const float step = float(1.0 / (kRampSeconds * m_rate));
    if (paused && m_gain <= 0) {
        std::memset(data, 0, size_t(out.size()) * sizeof(float));
        return;
    }
    // While pausing, keep reading until the fade-out is done.
    const qsizetype got = m_ring->read(data, frames);
    if (got < frames) {
        std::memset(data + got * 2, 0, size_t(frames - got) * 2 * sizeof(float));
        if (!paused)
            m_underruns.fetch_add(1, std::memory_order_relaxed);
    }
    const float volume = m_volume.load(std::memory_order_relaxed);
    for (qsizetype i = 0; i < got; ++i) {
        m_gain = paused ? std::max(0.f, m_gain - step) : std::min(1.f, m_gain + step);
        m_applied += (volume - m_applied) * 0.001f;
        // Perceptual volume: a cubic curve over the linear slider.
        const float g = m_gain * m_applied * m_applied * m_applied;
        data[i * 2] *= g;
        data[i * 2 + 1] *= g;
    }
    m_consumed.fetch_add(quint64(got), std::memory_order_relaxed);
}

std::pair<int, qint64> Player::position() const
{
    if (!m_sink)
        return {0, 0};
    // Frames handed to the device minus its buffer (an estimate of the output latency).
    const qint64 queued = m_sink->bufferFrameCount();
    const qint64 played = std::max<qint64>(0, qint64(m_consumed.load()) - queued);
    auto* self = const_cast<Player*>(this);
    std::lock_guard lock(self->m_lock);
    int track = m_baseTrack;
    qint64 since = played;
    qint64 base = m_baseMs;
    for (size_t i = 1; i < m_boundaries.size(); ++i) { // [0] is where the restart began
        const auto& b = m_boundaries[i];
        if (b.outFrame <= played) {
            track = b.track;
            since = played - b.outFrame;
            base = 0;
        }
    }
    return {track, base + since * 1000 / m_rate};
}

void Player::printStatus()
{
    const auto [track, ms] = position();
    std::printf("track %d  %02lld:%06.3f  ring %lld  underruns %llu%s\n", track + 1,
                static_cast<long long>(ms / 60000), double(ms % 60000) / 1000.0,
                static_cast<long long>(m_ring ? m_ring->readable() : 0),
                static_cast<unsigned long long>(m_underruns.load()),
                m_paused.load() ? "  (paused)" : "");
    std::fflush(stdout);
}

void Player::togglePause() { m_paused.store(!m_paused.load()); }

void Player::seek(qint64 ms) { restart(position().first, ms); }

void Player::next()
{
    const int track = position().first + 1;
    if (track < m_tracks.size())
        restart(track, 0);
}

void Player::devicesChanged()
{
    const QAudioDevice now = QMediaDevices::defaultAudioOutput();
    const bool gone = !m_device.isNull() && !QMediaDevices::audioOutputs().contains(m_device);
    if (now.id() == m_device.id() && !gone)
        return;
    std::printf("default output changed -> %s\n", qPrintable(now.description()));
    const auto [track, ms] = position();
    closeSink();
    if (now.isNull()) {
        m_paused.store(true); // nothing to play on; don't start by itself later
        return;
    }
    openSink(now, track, ms);
}

} // namespace poc
