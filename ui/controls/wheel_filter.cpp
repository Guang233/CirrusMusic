#include "wheel_filter.h"

#include <QWheelEvent>

namespace cirrus::ui
{

WheelFilter::WheelFilter(QObject* parent) : QObject(parent) {}

void WheelFilter::setTarget(QQuickItem* target)
{
    if (m_target == target)
        return;
    if (m_target)
        m_target->removeEventFilter(this);
    m_target = target;
    if (m_target)
        m_target->installEventFilter(this);
    emit targetChanged();
}

void WheelFilter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

bool WheelFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_target || event->type() != QEvent::Wheel || !m_enabled)
        return false;
    const auto* wheel = static_cast<QWheelEvent*>(event);
    // A wheel notch: no pixel delta, no scroll phase (touchpads have both), vertical, and
    // not Alt (which turns the wheel sideways).
    if (!wheel->pixelDelta().isNull() || wheel->phase() != Qt::NoScrollPhase ||
        wheel->angleDelta().y() == 0 || (wheel->modifiers() & Qt::AltModifier))
        return false;
    m_accepted = false;
    emit notch(wheel->angleDelta().y() / 120.0);
    if (!m_accepted)
        return false;
    event->accept();
    return true;
}

} // namespace cirrus::ui
