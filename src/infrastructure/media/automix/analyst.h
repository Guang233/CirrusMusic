#pragma once
#include <QObject>
#include <QString>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace cirrus::media
{
class ByteSource;
}

namespace cirrus::media::automix
{
struct Analysis;

// Analyses files on a low-priority thread of its own, one at a time
// (docs/tasks/automix.md §4.1), with a decoder of its own: never the one playing. An
// owner-thread object; results come back there.
class Analyst : public QObject
{
    Q_OBJECT
  public:
    explicit Analyst(QObject* parent = nullptr);
    ~Analyst() override;

    // Analyses `stream` ("mp3" or "flac"): all of it, or only its first `untilMs` (> 0).
    // Reads block until the bytes are there; canceling the stream ends the job.
    void analyse(quint64 id, std::shared_ptr<ByteSource> stream, const QString& format,
                 qint64 untilMs = 0);
    // A queued job goes; a running one's result is dropped.
    void cancel(quint64 id);

  signals:
    // Nothing when it failed (or the stream was canceled). `elapsedMs`: decoding included.
    void analysed(quint64 id, std::shared_ptr<const Analysis> analysis, qint64 elapsedMs);

  private:
    struct Job {
        quint64 id = 0;
        std::shared_ptr<ByteSource> stream;
        QString format;
        qint64 untilMs = 0;
    };
    void work();
    void finished(quint64 id, std::shared_ptr<const Analysis> analysis, qint64 elapsedMs);

    std::mutex m_lock;
    std::condition_variable m_wake;
    std::deque<Job> m_jobs;
    quint64 m_running = 0; // the job being analysed (0: none)
    bool m_quit = false;
    std::thread m_thread;
};

} // namespace cirrus::media::automix
