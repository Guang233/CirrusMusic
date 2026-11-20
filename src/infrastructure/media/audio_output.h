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

// What becomes of the audio still queued at a flush, while playing: it fades out under
// whatever comes next (FadeTail) instead of being cut.
enum class FlushTail {
    None, // dropped
    Cut,  // over a few ms, against a click (a seek, a skip in the gapless mode)
    Skip, // the quick fade of a skip (docs/tasks/automix.md §5.3)
};

// What was queued when a track was cut short (a skip), fading out over what plays next
// (docs/tasks/automix.md §5.3): the consumer's side of AudioOutput's flush, on its own to
// be testable. Real-time safe: the storage comes with the constructor.
class FadeTail final
{
  public:
    explicit FadeTail(qsizetype capacity = 0) : m_data(size_t(capacity) * 2) {}
    // Up to `frames` frames from `ring` (as many as fit) start fading from full gain. What
    // is left of the last tail (skips in a row) goes on inside the new one, at the gain it
    // had reached.
    void take(AudioRing& ring, qsizetype frames);
    // Adds the next `frames` frames into `data` (interleaved stereo) at `gain`; `limit`:
    // new audio plays under it, the sum goes through the soft limiter.
    void mix(float* data, qsizetype frames, float gain, bool limit);
    void clear() { m_at = m_length = 0; }
    [[nodiscard]] bool active() const { return m_at < m_length; }
    [[nodiscard]] qsizetype capacity() const { return qsizetype(m_data.size() / 2); }

  private:
    [[nodiscard]] float fade(qsizetype at) const;

    std::vector<float> m_data;
    qsizetype m_length = 0, m_at = 0;
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

    // Producer thread (or the owner with it stopped): stop the consumer reading, empty the
    // ring, refill, go on.
    void beginFlush(FlushTail tail = FlushTail::None);
    void endFlush();

    void setPlaying(bool playing); // false fades out and then plays silence
    void setGain(float linear);
    // Frames played out since the last endFlush(), and the device's own buffer.
    [[nodiscard]] quint64 consumed() const { return m_consumed.load(std::memory_order_relaxed); }
    [[nodiscard]] qint64 latencyFrames() const;
    [[nodiscard]] quint64 underruns() const { return m_underruns.load(std::memory_order_relaxed); }
    // Silent after a pause's fade-out.
    [[nodiscard]] bool silent() const { return m_silent.load(std::memory_order_relaxed); }
    // A skip's tail still fading out (don't suspend yet).
    [[nodiscard]] bool fading() const { return m_fading.load(std::memory_order_relaxed); }
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
    std::atomic<bool> m_fading{false};
    std::atomic<float> m_target{0.5f};
    std::atomic<quint64> m_consumed{0};
    std::atomic<quint64> m_underruns{0};
    std::atomic<qsizetype> m_tailWanted{0}; // frames for the consumer to keep at the flush
    // Consumer (real-time thread) only.
    float m_ramp = 0;
    float m_gain = 0;
    FadeTail m_tail;
};
} // namespace cirrus::media
