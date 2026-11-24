#include "desktop.h"

#include "gpu_preference.h"
#include "single_instance.h"
#ifdef CIRRUS_UI_TRAY
#include "tray.h"
#endif

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QWindow>

namespace cirrus::ui
{

namespace
{
Desktop* s_instance = nullptr;
} // namespace

Desktop::Desktop(SingleInstance* instance, QObject* parent) : QObject(parent)
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    QCoreApplication::instance()->installEventFilter(this);
    // Closing the window decides itself whether the app quits (Main.qml).
    QGuiApplication::setQuitOnLastWindowClosed(false);
    connect(instance, &SingleInstance::activateRequested, this, [this](const QString& token) {
        // Wayland raises the window only with the launcher's activation token; the
        // shell integration reads it when the window is shown.
        if (!token.isEmpty())
            qputenv("XDG_ACTIVATION_TOKEN", token.toLatin1());
        emit showRequested();
    });
    checkTray();
}

Desktop::~Desktop() { s_instance = nullptr; }

Desktop* Desktop::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/)
{
    Q_ASSERT(s_instance);
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

bool Desktop::switchableGraphics() const { return gpu::switchable(); }

bool Desktop::checkTray()
{
    bool available = false;
#ifdef CIRRUS_UI_TRAY
    available = Tray::available();
    if (available && !m_tray) {
        m_tray = new Tray(this);
        connect(m_tray, &Tray::showRequested, this, &Desktop::showRequested);
        connect(m_tray, &Tray::quitRequested, this, &Desktop::quit);
        m_tray->show();
    }
#endif
    if (available != m_trayAvailable) {
        m_trayAvailable = available;
        emit trayAvailableChanged();
    }
    return available;
}

void Desktop::quit()
{
    if (m_confirmQuit && m_confirmQuit()) {
        emit quitConfirmationRequested();
        return;
    }
    QCoreApplication::quit();
}

void Desktop::quitNow() { QCoreApplication::quit(); }

void Desktop::setQuitConfirmation(std::function<bool()> needed)
{
    m_confirmQuit = std::move(needed);
}

void Desktop::setShutdown(std::function<void(std::function<void()>)> shutdown)
{
    m_shutdown = std::move(shutdown);
}

bool Desktop::eventFilter(QObject* watched, QEvent* event)
{
    // QCoreApplication::quit() and the session ending both close the windows first.
    if (watched == QCoreApplication::instance() && event->type() == QEvent::Quit) {
        if (!m_quitting) {
            m_quitting = true;
            emit quittingChanged();
        }
        if (m_shutdown && m_shutdownStage < 2) {
            // Held until the shutdown is done; the windows go away meanwhile.
            if (m_shutdownStage == 0) {
                m_shutdownStage = 1;
                for (QWindow* window : QGuiApplication::topLevelWindows())
                    window->hide();
                m_shutdown([this] {
                    m_shutdownStage = 2;
                    QCoreApplication::quit();
                });
            }
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace cirrus::ui
