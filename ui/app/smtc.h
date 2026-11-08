#pragma once

#include <QObject>
#include <QTimer>

#include <memory>

class QWindow;

namespace cirrus::ui
{

class Player;

// Windows System Media Transport Controls (media keys, the volume flyout, the lock
// screen), driven by App.player like the window is (docs/ui/pages/shell.md §7). Bound to
// the main window; does nothing when the controls are unavailable. Not yet verified on
// Windows.
class Smtc : public QObject
{
    Q_OBJECT
  public:
    Smtc(Player* player, QWindow* window, QObject* parent = nullptr);
    ~Smtc() override;

    // Whether the controls were obtained.
    [[nodiscard]] bool available() const { return m_native != nullptr; }

  private:
    struct Native;

    void update();

    Player* m_player;
    std::unique_ptr<Native> m_native;
    QTimer m_tick; // refreshes the timeline while playing
};

} // namespace cirrus::ui
