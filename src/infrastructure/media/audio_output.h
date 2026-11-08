#pragma once
#include <QAudioDevice>
#include <QObject>
#include <QSpan>
#include <atomic>
#include <memory>
#include <vector>

class QAudioSink;
class QMediaDevices;

namespace cirrus::media
{

// Single-producer single-consumer ring of interleaved stereo float frames.
class AudioRing final
{
  public:
    explicit AudioRing(qsizetype frames) : m_data(size_t(frames) * 2), m_frames(frames) {}
    [[nodiscard]] qsizetype readable() const
    {
        return qsizetype(m_write.load(std::memory_order_acquire) -
                         m_read.load(std::memory_order_relaxed));
    }
    [[nodiscard]] qsizetype writable() const
    {
        return m_frames - qsizetype(m_write.load(std::memory_order_relaxed) -
                                    m_read.load(std::memory_order_acquire));
    }
    // Producer side.
    qsizetype write(const float* in, qsizetype frames);
    // Consumer side.
    qsizetype read(float* out, qsizetype frames);
    // Only while the consumer stays out (see AudioOutput's flush).
    void drop()
    {
        m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release);
    }

  private:
    std::vector<float> m_data;
    qsizetype m_frames;
    std::atomic<quint64> m_write{0}, m_read{0};
};

// The default output device through QAudioSink's real-time callback, which only copies
// out of the ring and applies short gain ramps (pause, seek) and the volume. Owner-thread
// object; the ring and the flush handshake are for the producer thread.
class AudioOutput final : public QObject
{
    Q_OBJECT
  public:
    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput() override;

    // (Re)opens the default device; false when there is none. Keeps the ring when the
    // rate stays, otherwise makes a new one (the producer restarts anyway).
    bool open();
    void close();
    [[nodiscard]] bool isOpen() const { return m_sink != nullptr; }
    [[nodiscard]] int sampleRate() const { return m_rate; }
    [[nodiscard]] AudioRing* ring() const { return m_ring.get(); }

    // Producer thread: stop the consumer reading, empty the ring, refill, go on.
    void beginFlush();
    void endFlush();

    void setPlaying(bool playing); // false fades out and then plays silence
    void setGain(float linear);
    // Frames played out since the last endFlush(), and the device's own buffer.
    [[nodiscard]] quint64 consumed() const { return m_consumed.load(std::memory_order_relaxed); }
    [[nodiscard]] qint64 latencyFrames() const;
    [[nodiscard]] quint64 underruns() const { return m_underruns.load(std::memory_order_relaxed); }
    // Silent after a pause's fade-out.
    [[nodiscard]] bool silent() const { return m_silent.load(std::memory_order_relaxed); }
    void suspend();
    void resume();

  signals:
    // The default device changed or ours went away; the engine reopens and restarts.
    void deviceChanged();

  private:
    void consume(QSpan<float> out);
    void devicesChanged();

    QMediaDevices* m_devices = nullptr;
    std::unique_ptr<QAudioSink> m_sink;
    QAudioDevice m_device;
    int m_rate = 0;
    std::unique_ptr<AudioRing> m_ring;
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_flush{false};
    std::atomic<bool> m_flushSeen{false};
    std::atomic<bool> m_silent{true};
    std::atomic<float> m_target{0.5f};
    std::atomic<quint64> m_consumed{0};
    std::atomic<quint64> m_underruns{0};
    // Consumer (real-time thread) only.
    float m_ramp = 0;
    float m_gain = 0;
};
} // namespace cirrus::media
