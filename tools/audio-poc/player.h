#pragma once

#include "chain.h"

#include <QAudioDevice>
#include <QObject>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

class QAudioSink;

namespace poc
{

// Single-producer single-consumer ring of interleaved stereo float frames.
class Ring
{
  public:
    explicit Ring(qsizetype frames) : m_data(size_t(frames) * 2), m_frames(frames) {}
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
    void write(const float* in, qsizetype frames);
    qsizetype read(float* out, qsizetype frames);
    void drop()
    {
        m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release);
    }

  private:
    std::vector<float> m_data;
    qsizetype m_frames;
    std::atomic<quint64> m_write{0}, m_read{0};
};

// The playing side of the PoC: a producer thread decodes into the ring, the sink's
// real-time callback copies out of it with short gain ramps for pause/seek.
class Player : public QObject
{
    Q_OBJECT
  public:
    explicit Player(QStringList tracks, QObject* parent = nullptr);
    ~Player() override;

    bool start();
    void togglePause();
    void seek(qint64 ms);
    void next();
    void setVolume(float volume) { m_volume.store(volume); }
    void printStatus();

  private:
    void openSink(const QAudioDevice& device, int track, qint64 ms);
    void closeSink();
    void produce();
    void consume(QSpan<float> out);
    void devicesChanged();
    // Current track and position from what the sink has consumed.
    std::pair<int, qint64> position() const;
    void restart(int track, qint64 ms);

    QStringList m_tracks;
    std::unique_ptr<QAudioSink> m_sink;
    QAudioDevice m_device;
    int m_rate = 48000;

    // Producer state, guarded by m_lock.
    std::mutex m_lock;
    std::condition_variable m_wake;
    std::unique_ptr<Chain> m_chain;
    bool m_quit = false;
    bool m_restart = false;
    int m_restartTrack = 0;
    qint64 m_restartMs = 0;
    std::vector<Chain::Boundary> m_boundaries; // copy for the main thread
    int m_baseTrack = 0;
    qint64 m_baseMs = 0;
    QThread* m_producer = nullptr;

    std::unique_ptr<Ring> m_ring;
    // Consumer (real-time) side.
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_flush{false};     // producer refills: the consumer leaves the ring alone
    std::atomic<bool> m_flushSeen{false}; // the consumer is out of the ring
    std::atomic<quint64> m_consumed{0};   // frames played since the last restart
    std::atomic<quint64> m_underruns{0};
    std::atomic<float> m_volume{0.5f};
    float m_gain = 0;    // current ramp gain (consumer only)
    float m_applied = 0; // smoothed volume (consumer only)
};

} // namespace poc
