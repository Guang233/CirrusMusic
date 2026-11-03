#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Whether the last input came from the keyboard (a key other than a modifier) or a
// pointer (press, touch). Focus indicators that a click may also move (list rows taking
// focus back after a box closes, a menu returning focus) show only with the keyboard:
//   FocusRing { shown: InputMode.keyboard && row.activeFocus }
// The singleton is created on first use and only sees input from then on: read it
// first in such bindings, so it exists before the row has the focus.
class InputMode : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool keyboard READ keyboard NOTIFY keyboardChanged)

  public:
    explicit InputMode(QObject* parent = nullptr);

    bool keyboard() const { return m_keyboard; }

  signals:
    void keyboardChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void setKeyboard(bool keyboard);

    bool m_keyboard = false;
};

} // namespace cirrus::ui
