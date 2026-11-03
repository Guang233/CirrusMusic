#include "input_mode.h"

#include <QCoreApplication>
#include <QKeyEvent>

namespace cirrus::ui
{

InputMode::InputMode(QObject* parent) : QObject(parent)
{
    QCoreApplication::instance()->installEventFilter(this);
}

bool InputMode::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
        switch (static_cast<QKeyEvent*>(event)->key()) {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
        case Qt::Key_AltGr:
            break;
        default:
            setKeyboard(true);
        }
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
        setKeyboard(false);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void InputMode::setKeyboard(bool keyboard)
{
    if (m_keyboard == keyboard)
        return;
    m_keyboard = keyboard;
    emit keyboardChanged();
}

} // namespace cirrus::ui
