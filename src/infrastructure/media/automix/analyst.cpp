#include "analyst.h"
#include "analysis.h"
#include "infrastructure/media/audio_decoder.h"
#include "infrastructure/media/byte_source.h"
#include <QElapsedTimer>
#include <QThread>
#if defined(Q_OS_LINUX)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace cirrus::media::automix
{
namespace
{
constexpr qsizetype kChunkFrames = 4096;
}

Analyst::Analyst(QObject* parent) : QObject(parent), m_thread([this] { work(); }) {}

Analyst::~Analyst()
{
    {
        std::lock_guard guard(m_lock);
        m_quit = true;
        m_jobs.clear();
    }
    m_wake.notify_all();
    // A read waiting for a download ends when its owner cancels the stream (it does before
    // this: the engine stops first).
    m_thread.join();
}

void Analyst::analyse(quint64 id, std::shared_ptr<ByteSource> stream, const QString& format,
                      qint64 untilMs)
{
    {
        std::lock_guard guard(m_lock);
        m_jobs.push_back({id, std::move(stream), format, untilMs});
    }
    m_wake.notify_one();
}

void Analyst::cancel(quint64 id)
{
    std::lock_guard guard(m_lock);
    std::erase_if(m_jobs, [id](const Job& job) { return job.id == id; });
    if (m_running == id)
        m_running = 0; // its result is dropped
}

void Analyst::finished(quint64 id, std::shared_ptr<const Analysis> analysis, qint64 elapsedMs)
{
    {
        std::lock_guard guard(m_lock);
        if (m_running != id)
            return; // canceled meanwhile
        m_running = 0;
    }
    emit analysed(id, std::move(analysis), elapsedMs);
}

void Analyst::work()
{
    QThread::currentThread()->setObjectName(QStringLiteral("analysis")); // in the log
#if defined(Q_OS_LINUX)
    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 10); // under playback and the UI
#endif
    while (true) {
        Job job;
        {
            std::unique_lock guard(m_lock);
            m_wake.wait(guard, [this] { return m_quit || !m_jobs.empty(); });
            if (m_quit)
                return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            m_running = job.id;
        }
        QElapsedTimer timer;
        timer.start();
        std::shared_ptr<const Analysis> result;
        AudioDecoder decoder;
        if (decoder.open(job.stream, job.format)) {
            Analyser analyser(decoder.sampleRate());
            std::vector<float> chunk;
            bool ended = false;
            while (true) {
                {
                    std::lock_guard guard(m_lock);
                    if (m_quit || m_running != job.id)
                        break;
                }
                if (job.untilMs > 0 && analyser.durationMs() >= job.untilMs)
                    break;
                chunk.clear();
                if (!decoder.read(chunk, kChunkFrames)) {
                    ended = !decoder.error() && !decoder.interrupted();
                    break;
                }
                analyser.add(chunk.data(), qsizetype(chunk.size() / 2));
            }
            if (ended || (job.untilMs > 0 && analyser.durationMs() >= job.untilMs))
                result = std::make_shared<const Analysis>(analyser.finish(ended));
        }
        job.stream.reset(); // the handle goes here, not with the owner's later
        QMetaObject::invokeMethod(
            this,
            [this, id = job.id, result, elapsed = timer.elapsed()] {
                finished(id, result, elapsed);
            },
            Qt::QueuedConnection);
    }
}

} // namespace cirrus::media::automix
