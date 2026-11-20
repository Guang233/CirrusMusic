#pragma once
#include "logging/log.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>

// One summary record for many operations, in place of a record each (docs/tasks/logging-v2.md
// §3): how many, how many failed and how, how long they took.
//   m_stats.add(ms, path, failed ? logName(error) : QString());
//   if (m_stats.due()) log::info(lcApi, "api.stats", m_stats.take());
namespace cirrus::log
{
class Durations
{
  public:
    explicit Durations(qint64 windowMs = 5 * 60 * 1000);

    // `name` (the operation) is reported when it was the slowest; `error` is empty for
    // a success.
    void add(qint64 ms, const QString& name, const QString& error = {});
    // The window is over and something was counted.
    [[nodiscard]] bool due() const;
    [[nodiscard]] bool empty() const { return m_ms.isEmpty(); }
    // n, failed, errors ("Network:3,Timeout:1"), p50_ms, p95_ms, max_ms, slowest, window_s;
    // then starts a new window.
    Fields take();

  private:
    qint64 m_windowMs;
    QElapsedTimer m_window;
    QList<qint64> m_ms; // at most kSamples, for the percentiles
    qint64 m_count = 0;
    qint64 m_max = -1;
    QString m_slowest;
    QHash<QString, qint64> m_errors;
};

// Failures that come together (an image host down, a connection that stalls): each one a
// debug record, and one warning when they pile up, not one each.
//   log::debug(lcCache, "cover.failed", fields);
//   if (m_burst.add()) log::warn(lcCache, "cover.failing", {{"failed", m_burst.count()}, fields});
class Burst
{
  public:
    Burst(int threshold, qint64 windowMs) : m_threshold(threshold), m_windowMs(windowMs) {}

    // One more failure; true for the `threshold`th within `windowMs` of the window's first,
    // then not again until a failure after the window starts a new one.
    bool add()
    {
        if (!m_window.isValid() || m_window.elapsed() >= m_windowMs) {
            m_window.start();
            m_count = 0;
        }
        return ++m_count == m_threshold;
    }
    // The failures in this window so far.
    [[nodiscard]] int count() const { return m_count; }

  private:
    int m_threshold;
    qint64 m_windowMs;
    QElapsedTimer m_window;
    int m_count = 0;
};
} // namespace cirrus::log
