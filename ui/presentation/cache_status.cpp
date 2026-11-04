#include "cache_status.h"

#include "sources.h"

namespace cirrus::ui
{

CacheStatus::CacheStatus(CacheSource* source, QObject* parent) : QObject(parent), m_source(source)
{
}

void CacheStatus::refresh()
{
    if (!m_source)
        return;
    m_usage = m_source->usage([this](Outcome<CacheUsage> outcome) {
        m_usage.release();
        m_bytes = outcome.value ? outcome.value->bytes : -1;
        m_limitBytes = outcome.value ? outcome.value->limitBytes : -1;
        emit usageChanged();
    });
}

void CacheStatus::clear()
{
    if (!m_source || m_clearing.pending())
        return;
    m_clearing = m_source->clear([this](Outcome<bool> outcome) {
        m_clearing.release();
        emit clearingChanged();
        const bool ok = outcome.value.value_or(false);
        emit cleared(ok);
        refresh();
    });
    emit clearingChanged();
}

} // namespace cirrus::ui
