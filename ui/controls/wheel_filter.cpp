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
    // A wheel notch: no scroll phase (touchpad gestures on Wayland and macOS begin, update
    // and end), vertical, and not Alt (which turns the wheel sideways). Not the pixel
    // delta: Qt 6.12's Wayland plugin gives wheel notches one too (15 px), and handing
    // those to the Flickable scrolled 15 px a notch.
    if (wheel->phase() != Qt::NoScrollPhase || wheel->angleDelta().y() == 0 ||
        (wheel->modifiers() & Qt::AltModifier))
        return false;
    m_accepted = false;
    emit notch(wheel->angleDelta().y() / 120.0);
    if (!m_accepted)
        return false;
    event->accept();
    return true;
}

} // namespace cirrus::ui
