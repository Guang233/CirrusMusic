#pragma once

#include "application/account/login_scheduler.h"

#include <algorithm>
#include <vector>

namespace cirrus::test
{

// Virtual monotonic time. Nothing fires unless the test calls advance()/deliverDue();
// jump() moves time without delivery to model a busy event loop / late timer.
class FakeLoginScheduler final : public application::ILoginScheduler
{
  public:
    using Ms = std::chrono::milliseconds;
    struct Timer {
        quint64 id = 0;
        Ms due{};
        std::function<void()> callback;
    };

    // When true, stop() is ignored: models a callback already dequeued before stop.
    bool ignoreStop = false;
    int started = 0;
    int stopped = 0;

    [[nodiscard]] Ms now() const override { return m_now; }
    [[nodiscard]] quint64 start(Ms delay, std::function<void()> callback) override
    {
        ++started;
        m_timers.push_back({++m_next, m_now + std::max(delay, Ms(0)), std::move(callback)});
        return m_next;
    }
    void stop(quint64 id) override
    {
        ++stopped;
        if (ignoreStop)
            return;
        std::erase_if(m_timers, [id](const Timer& t) { return t.id == id; });
    }

    [[nodiscard]] qsizetype pending() const { return qsizetype(m_timers.size()); }
    [[nodiscard]] bool hasDueAt(Ms at) const
    {
        return std::any_of(m_timers.begin(), m_timers.end(),
                           [at](const Timer& t) { return t.due == at; });
    }
    void jump(Ms delta) { m_now += delta; }

    // Deliver every timer due at or before now, in (due, id) order; includes timers
    // started by callbacks if they are already due. Each runs with now unchanged.
    int deliverDue() { return runUntil(m_now); }

    // Advance in steps, firing each timer at its own due time, then land on target.
    int advance(Ms delta)
    {
        const auto target = m_now + delta;
        const int fired = runUntil(target);
        m_now = std::max(m_now, target);
        return fired;
    }

  private:
    int runUntil(Ms limit)
    {
        int fired = 0;
        for (;;) {
            auto it = std::min_element(m_timers.begin(), m_timers.end(),
                                       [](const Timer& a, const Timer& b) {
                                           return a.due != b.due ? a.due < b.due : a.id < b.id;
                                       });
            if (it == m_timers.end() || it->due > limit)
                return fired;
            auto timer = std::move(*it);
            m_timers.erase(it);
            m_now = std::max(m_now, timer.due);
            ++fired;
            timer.callback();
        }
    }

    Ms m_now{0};
    quint64 m_next = 0;
    std::vector<Timer> m_timers;
};

} // namespace cirrus::test
