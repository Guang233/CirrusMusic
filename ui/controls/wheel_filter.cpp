#include "wheel_filter.h"

#include <QCoreApplication>
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

bool WheelFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_target || event->type() != QEvent::Wheel)
        return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    // A wheel notch has no scroll phase (touchpad gestures begin, update and end); only
    // one carrying a pixel delta needs the copy.
    if (wheel->phase() != Qt::NoScrollPhase || wheel->pixelDelta().isNull() ||
        wheel->angleDelta().isNull())
        return false;
    QWheelEvent notch(wheel->position(), wheel->globalPosition(), QPoint(), wheel->angleDelta(),
                      wheel->buttons(), wheel->modifiers(), Qt::NoScrollPhase, wheel->inverted(),
                      wheel->source(), wheel->pointingDevice());
    notch.setTimestamp(wheel->timestamp());
    notch.setAccepted(false);
    QCoreApplication::sendEvent(m_target, &notch);
    event->setAccepted(notch.isAccepted());
    return true;
}

} // namespace cirrus::ui
