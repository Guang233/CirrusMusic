#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Sees a scroll view's wheel events before the view does and lets QML take the mouse
// wheel's notches (docs/ui/pages/playlist.md §12), leaving everything else to the view:
// touchpad scrolling (pixel deltas, scroll phases) and the notches QML does not take
// (`accepted` stays false, e.g. at an end, so the view around it scrolls). A WheelHandler
// cannot do this: it marks the event handled before its `wheel` signal runs. Wheels are
// told apart by their events, not by the device: under some Wayland compositors (niri)
// Qt reports the one pointer, mouse wheel included, as a touchpad.
//   WheelFilter { target: list; onNotch: steps => { …; accepted = true } }
class WheelFilter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem* target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool accepted MEMBER m_accepted)

  public:
    explicit WheelFilter(QObject* parent = nullptr);

    QQuickItem* target() const { return m_target; }
    void setTarget(QQuickItem* target);
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

  signals:
    void targetChanged();
    void enabledChanged();
    // Vertical notches (120 per notch; positive scrolls up, fractions from fine wheels).
    // Set `accepted` to take the event.
    void notch(qreal steps);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QPointer<QQuickItem> m_target;
    bool m_enabled = true;
    bool m_accepted = false;
};

} // namespace cirrus::ui
