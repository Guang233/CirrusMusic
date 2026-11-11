#include "durations.h"

#include <algorithm>

namespace cirrus::log
{
namespace
{
constexpr qsizetype kSamples = 4096; // past this, every other sample is kept
}

Durations::Durations(qint64 windowMs) : m_windowMs(windowMs) { m_window.start(); }

void Durations::add(qint64 ms, const QString& name, const QString& error)
{
    ++m_count;
    if (m_ms.size() < kSamples || m_count % 2 == 0)
        m_ms.append(ms);
    if (m_ms.size() > kSamples)
        m_ms.removeFirst();
    if (ms > m_max) {
        m_max = ms;
        m_slowest = name;
    }
    if (!error.isEmpty())
        ++m_errors[error];
}

bool Durations::due() const { return !m_ms.isEmpty() && m_window.elapsed() >= m_windowMs; }

Text Durations::take()
{
    std::sort(m_ms.begin(), m_ms.end());
    const auto at = [this](double q) {
        return m_ms.isEmpty() ? 0 : m_ms[std::min(m_ms.size() - 1, qsizetype(q * m_ms.size()))];
    };
    qint64 failed = 0;
    QStringList errors;
    for (auto it = m_errors.cbegin(); it != m_errors.cend(); ++it) {
        failed += it.value();
        errors.append(it.key() + u':' + QString::number(it.value()));
    }
    errors.sort();
    const Text text =
        join({field("n", m_count), field("failed", failed),
              fieldIf(!errors.isEmpty(), "errors", errors.join(u',')), field("p50_ms", at(0.5)),
              field("p95_ms", at(0.95)), field("max_ms", std::max<qint64>(m_max, 0)),
              fieldIf(!m_slowest.isEmpty(), "slowest", m_slowest),
              field("window_s", m_window.elapsed() / 1000)});
    m_ms.clear();
    m_count = 0;
    m_max = -1;
    m_slowest.clear();
    m_errors.clear();
    m_window.restart();
    return text;
}
} // namespace cirrus::log
