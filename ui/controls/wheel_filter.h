#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Makes a scroll view treat mouse wheel notches as wheel notches (docs/ui/pages/playlist.md
// §12). Qt 6.12's Wayland plugin gives each notch a 15 px pixel delta as well; a Flickable
// then takes it for touchpad scrolling and moves 15 px a notch. The filter hands the view
// the same event without the pixel delta, so Qt's own wheel scrolling (wheelScrollLines ×
// 24 px a notch, overshooting and springing back at the ends) applies on every Qt version.
// Touchpad gestures (with a scroll phase) pass untouched.
//   WheelFilter { target: list }
class WheelFilter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem* target READ target WRITE setTarget NOTIFY targetChanged)

  public:
    explicit WheelFilter(QObject* parent = nullptr);

    QQuickItem* target() const { return m_target; }
    void setTarget(QQuickItem* target);

  signals:
    void targetChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QPointer<QQuickItem> m_target;
};

} // namespace cirrus::ui
