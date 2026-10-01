#include "login_scheduler.h"

#include <QThread>
#include <QTimerEvent>

#include <algorithm>
#include <utility>

namespace cirrus::application
{

QtLoginScheduler::QtLoginScheduler(QObject* parent) : QObject(parent) { m_clock.start(); }

QtLoginScheduler::~QtLoginScheduler()
{
    for (auto it = m_timers.cbegin(); it != m_timers.cend(); ++it) {
        killTimer(it.key());
    }
}

std::chrono::milliseconds QtLoginScheduler::now() const
{
    return std::chrono::milliseconds(m_clock.elapsed());
}

quint64 QtLoginScheduler::start(std::chrono::milliseconds delay, std::function<void()> callback)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const int qtId = startTimer(std::max(delay, std::chrono::milliseconds(0)), Qt::PreciseTimer);
    if (qtId == 0) {
        qFatal("QtLoginScheduler: unable to start timer");
    }
    const auto id = ++m_nextId;
    m_timers.insert(qtId, {id, std::move(callback)});
    m_qtTimerIds.insert(id, qtId);
    return id;
}

void QtLoginScheduler::stop(quint64 timerId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto qtId = m_qtTimerIds.take(timerId);
    if (qtId != 0) {
        killTimer(qtId);
        m_timers.remove(qtId);
    }
}

void QtLoginScheduler::timerEvent(QTimerEvent* event)
{
    const auto it = m_timers.find(event->timerId());
    if (it == m_timers.end()) {
        QObject::timerEvent(event);
        return;
    }
    // One-shot: fully unregister before running, the callback may reenter start/stop.
    killTimer(event->timerId());
    auto entry = std::move(*it);
    m_timers.erase(it);
    m_qtTimerIds.remove(entry.id);
    entry.callback();
}

} // namespace cirrus::application
