#include "cache_status.h"

#include "app_settings.h"
#include "sources.h"

#include <algorithm>

namespace cirrus::ui
{

namespace
{
constexpr qint64 kMinimumAudioLimit = qint64(1) << 29; // 0.5 GiB
constexpr qint64 kMaximumAudioLimit = qint64(50) << 30;
} // namespace

CacheStatus::CacheStatus(CacheSource* source, AppSettings* settings, QObject* parent)
    : QObject(parent), m_source(source)
{
    if (!m_source || !settings)
        return;
    const auto apply = [this, settings] {
        m_source->setAudioCapacity(
            std::clamp(settings->audioCacheLimit(), kMinimumAudioLimit, kMaximumAudioLimit));
    };
    apply();
    connect(settings, &AppSettings::audioCacheLimitChanged, this, [this, apply] {
        apply();
        refresh(); // lowering it may have removed songs
    });
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
    m_audioUsage = m_source->audioUsage([this](Outcome<qint64> outcome) {
        m_audioUsage.release();
        m_audioBytes = outcome.value.value_or(-1);
        emit audioUsageChanged();
    });
}

void CacheStatus::clearAudio()
{
    if (!m_source || m_audioClearing.pending())
        return;
    m_audioClearing = m_source->clearAudio([this](Outcome<qint64> outcome) {
        m_audioClearing.release();
        emit audioClearingChanged();
        if (outcome.value) {
            m_audioBytes = *outcome.value;
            emit audioUsageChanged();
        }
        emit audioCleared(outcome.value.has_value());
    });
    emit audioClearingChanged();
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
