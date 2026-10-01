#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>

#include <chrono>
#include <functional>

namespace cirrus::application
{

// Narrow monotonic time + one-shot timer seam for authentication pacing.
// Thread-affine: use only in the owning event-loop thread. Callbacks run later
// from the event loop, never synchronously inside start(). stop() of a pending
// timer guarantees its callback will not run. Not a general task scheduler.
class ILoginScheduler
{
  public:
    virtual ~ILoginScheduler() = default;
    // Monotonic, unrelated to wall clock; suspend/resume semantics are not promised.
    [[nodiscard]] virtual std::chrono::milliseconds now() const = 0;
    [[nodiscard]] virtual quint64 start(std::chrono::milliseconds delay,
                                        std::function<void()> callback) = 0;
    virtual void stop(quint64 timerId) = 0;
};

// Production implementation: QElapsedTimer + precise QObject timers (Qt Core only).
class QtLoginScheduler final : public QObject, public ILoginScheduler
{
    Q_OBJECT
  public:
    explicit QtLoginScheduler(QObject* parent = nullptr);
    ~QtLoginScheduler() override;
    [[nodiscard]] std::chrono::milliseconds now() const override;
    [[nodiscard]] quint64 start(std::chrono::milliseconds delay,
                                std::function<void()> callback) override;
    void stop(quint64 timerId) override;

  protected:
    void timerEvent(QTimerEvent* event) override;

  private:
    struct Entry {
        quint64 id = 0;
        std::function<void()> callback;
    };
    QElapsedTimer m_clock;
    QHash<int, Entry> m_timers;       // Qt timer IDs may be reused after kill
    QHash<quint64, int> m_qtTimerIds; // our never-reused IDs
    quint64 m_nextId = 0;
};

} // namespace cirrus::application
