#include "audio_output.h"
#include <QAudioSink>
#include <QMediaDevices>
#include <QThread>
#include <algorithm>
#include <cstring>

namespace cirrus::media
{
namespace
{
constexpr double kRingSeconds = 0.5;
constexpr double kRampSeconds = 0.008;   // pause/seek fade, no clicks
constexpr float kGainSmoothing = 0.002f; // per frame towards the volume
} // namespace

qsizetype AudioRing::write(const float* in, qsizetype frames)
{
    const quint64 at = m_write.load(std::memory_order_relaxed);
    const qsizetype take = std::min(frames, writable());
    for (qsizetype i = 0; i < take; ++i) {
        const size_t slot = size_t((at + quint64(i)) % quint64(m_frames)) * 2;
        m_data[slot] = in[i * 2];
        m_data[slot + 1] = in[i * 2 + 1];
    }
    m_write.store(at + quint64(take), std::memory_order_release);
    return take;
}

qsizetype AudioRing::read(float* out, qsizetype frames)
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

AudioOutput::AudioOutput(QObject* parent) : QObject(parent), m_devices(new QMediaDevices(this))
{
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &AudioOutput::devicesChanged);
}

AudioOutput::~AudioOutput() { close(); }

bool AudioOutput::open()
{
    close();
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return false;
    QAudioFormat format;
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    format.setSampleRate(device.preferredFormat().sampleRate());
    if (!device.isFormatSupported(format))
        format.setSampleRate(48000);
    if (!device.isFormatSupported(format))
        return false;
    if (!m_ring || format.sampleRate() != m_rate)
        m_ring = std::make_unique<AudioRing>(qsizetype(format.sampleRate() * kRingSeconds));
    m_rate = format.sampleRate();
    m_device = device;
    m_sink = std::make_unique<QAudioSink>(device, format);
    m_sink->setBufferFrameCount(m_rate / 50); // 20 ms
    m_sink->start([this](QSpan<float> out) { consume(out); });
    return true;
}

void AudioOutput::close()
{
    if (!m_sink)
        return;
    m_sink->stop();
    m_sink.reset();
}

qint64 AudioOutput::latencyFrames() const { return m_sink ? m_sink->bufferFrameCount() : 0; }

void AudioOutput::suspend()
{
    if (m_sink && m_sink->state() == QtAudio::ActiveState)
        m_sink->suspend();
}

void AudioOutput::resume()
{
    if (m_sink && m_sink->state() == QtAudio::SuspendedState)
        m_sink->resume();
}

void AudioOutput::beginFlush()
{
    m_flushSeen.store(false, std::memory_order_release);
    m_flush.store(true, std::memory_order_release);
    // The consumer answers within one period; a stopped or suspended sink never does.
    for (int i = 0; i < 100 && !m_flushSeen.load(std::memory_order_acquire); ++i)
        QThread::usleep(500);
    if (m_ring)
        m_ring->drop();
}

void AudioOutput::endFlush()
{
    m_consumed.store(0, std::memory_order_relaxed);
    m_flush.store(false, std::memory_order_release);
}

void AudioOutput::setPlaying(bool playing)
{
    if (playing)
        m_silent.store(false, std::memory_order_relaxed);
    m_playing.store(playing, std::memory_order_release);
}

void AudioOutput::setGain(float linear) { m_target.store(linear, std::memory_order_relaxed); }

void AudioOutput::consume(QSpan<float> out)
{
    float* data = out.data();
    const qsizetype frames = out.size() / 2;
    if (m_flush.load(std::memory_order_acquire) || !m_ring) {
        m_flushSeen.store(true, std::memory_order_release);
        m_ramp = 0; // fade in after it
        std::memset(data, 0, size_t(out.size()) * sizeof(float));
        return;
    }
    const bool playing = m_playing.load(std::memory_order_acquire);
    if (!playing && m_ramp <= 0) {
        m_silent.store(true, std::memory_order_relaxed);
        std::memset(data, 0, size_t(out.size()) * sizeof(float));
        return;
    }
    // While pausing, keep reading until the fade-out is done.
    const qsizetype got = m_ring->read(data, frames);
    if (got < frames) {
        std::memset(data + got * 2, 0, size_t(frames - got) * 2 * sizeof(float));
        if (playing)
            m_underruns.fetch_add(1, std::memory_order_relaxed);
    }
    const float step = float(1.0 / (kRampSeconds * m_rate));
    const float target = m_target.load(std::memory_order_relaxed);
    for (qsizetype i = 0; i < got; ++i) {
        m_ramp = playing ? std::min(1.f, m_ramp + step) : std::max(0.f, m_ramp - step);
        m_gain += (target - m_gain) * kGainSmoothing;
        const float g = m_ramp * m_gain;
        data[i * 2] *= g;
        data[i * 2 + 1] *= g;
    }
    m_consumed.fetch_add(quint64(got), std::memory_order_relaxed);
}

void AudioOutput::devicesChanged()
{
    const QAudioDevice now = QMediaDevices::defaultAudioOutput();
    const bool gone = !m_device.isNull() && !QMediaDevices::audioOutputs().contains(m_device);
    if (now.id() == m_device.id() && !gone)
        return;
    emit deviceChanged();
}
} // namespace cirrus::media
