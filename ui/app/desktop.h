#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace cirrus::ui
{

class SingleInstance;
class Tray;

// Desktop integration for Main.qml (`Desktop.…`): the tray icon, other launches
// asking for the window, and quitting. main.cpp creates it after the application
// object; see docs/ui/pages/shell.md §7.
class Desktop : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Whether a tray icon is shown, so closing the window can keep the app running.
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    // Set once the app is quitting (the tray's Quit, Qt.quit(), the session ending):
    // closing the window must not be turned into hiding then.
    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged)
    // Whether the "use the discrete GPU" setting does anything here.
    Q_PROPERTY(bool switchableGraphics READ switchableGraphics CONSTANT)

  public:
    explicit Desktop(SingleInstance* instance, QObject* parent = nullptr);
    ~Desktop() override;

    // QML singleton factory: the instance main.cpp created, owned by C++.
    static Desktop* create(QQmlEngine* qml, QJSEngine* js);

    [[nodiscard]] bool trayAvailable() const { return m_trayAvailable; }
    [[nodiscard]] bool quitting() const { return m_quitting; }
    [[nodiscard]] bool switchableGraphics() const;

    // Checks the tray again (a panel can start after the app) and returns
    // trayAvailable. Call before hiding the window.
    Q_INVOKABLE bool checkTray();
    Q_INVOKABLE void quit();

  signals:
    void trayAvailableChanged();
    void quittingChanged();
    // The tray or another launch wants the window shown and raised.
    void showRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    Tray* m_tray = nullptr;
    bool m_trayAvailable = false;
    bool m_quitting = false;
};

} // namespace cirrus::ui
