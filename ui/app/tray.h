#pragma once

#include <QObject>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace cirrus::ui
{

// The tray icon and its menu (docs/10-linux-integration.md §4): the current song,
// play/pause, previous, next, show the window, quit. A click shows the window, a
// middle click plays or pauses. Needs QApplication; built with CIRRUS_UI_TRAY.
class Tray : public QObject
{
    Q_OBJECT

  public:
    explicit Tray(QObject* parent = nullptr);
    ~Tray() override;

    // Whether the desktop shows tray icons right now (GNOME does not without an
    // extension). Checked live: a panel can start after the app.
    [[nodiscard]] static bool available();
    void show();

  signals:
    void showRequested();
    void quitRequested();

  private:
    void update();

    std::unique_ptr<QMenu> m_menu;
    QSystemTrayIcon* m_icon;
    QAction* m_song;
    QAction* m_playPause;
    QAction* m_previous;
    QAction* m_next;
};

} // namespace cirrus::ui
