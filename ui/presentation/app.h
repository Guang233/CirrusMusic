#pragma once

#include "sources.h"

#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace cirrus::ui
{

class AppSettings;
class FakeControls;
class Formatter;
class Library;
class NetworkStatus;
class Player;
class Session;

// Root of the presentation layer for QML (`import Cirrus.Presentation`, then `App.…`).
// main.cpp installs the data sources before loading QML:
//   cirrus::ui::App::install(cirrus::ui::makeFakeSources());
//   engine.addImageProvider("cover", cirrus::ui::App::instance()->makeCoverProvider());
// Page ViewModels take their sources from here. All on the GUI thread.
class App : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("app_settings.h")
    Q_MOC_INCLUDE("fake_controls.h")
    Q_MOC_INCLUDE("formatter.h")
    Q_MOC_INCLUDE("library.h")
    Q_MOC_INCLUDE("network_status.h")
    Q_MOC_INCLUDE("player.h")
    Q_MOC_INCLUDE("session.h")
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(cirrus::ui::AppSettings* settings READ settings CONSTANT)
    Q_PROPERTY(cirrus::ui::Formatter* format READ format CONSTANT)
    Q_PROPERTY(cirrus::ui::Library* library READ library CONSTANT)
    Q_PROPERTY(cirrus::ui::NetworkStatus* network READ network CONSTANT)
    Q_PROPERTY(cirrus::ui::Player* player READ player CONSTANT)
    Q_PROPERTY(cirrus::ui::Session* session READ session CONSTANT)
    // Fake source knobs; null with real sources.
    Q_PROPERTY(cirrus::ui::FakeControls* debug READ debug CONSTANT)

  public:
    ~App() override;

    static void install(Sources sources);
    // Null before install().
    [[nodiscard]] static App* instance();
    // QML singleton factory: the installed instance, owned by C++.
    static App* create(QQmlEngine* qml, QJSEngine* js);

    [[nodiscard]] AppSettings* settings() const { return m_settings; }
    [[nodiscard]] Formatter* format() const { return m_format; }
    [[nodiscard]] Library* library() const { return m_library; }
    [[nodiscard]] NetworkStatus* network() const { return m_network; }
    [[nodiscard]] Player* player() const { return m_player; }
    [[nodiscard]] Session* session() const { return m_session; }
    [[nodiscard]] FakeControls* debug() const { return m_sources.fakeControls.get(); }

    [[nodiscard]] CatalogSource* catalog() const { return m_sources.catalog.get(); }
    [[nodiscard]] ImageSource* images() const { return m_sources.images.get(); }
    [[nodiscard]] LibrarySource* librarySource() const { return m_sources.library.get(); }

    // `image://cover/<key>` provider; the QML engine takes ownership.
    [[nodiscard]] QQuickAsyncImageProvider* makeCoverProvider();
    // `image://qr/<serial>` provider for the sign-in code; the engine takes ownership.
    [[nodiscard]] QQuickImageProvider* makeQrProvider();

  private:
    explicit App(Sources sources);

    Sources m_sources;
    AppSettings* m_settings;
    Formatter* m_format;
    NetworkStatus* m_network;
    Player* m_player;
    Session* m_session;
    Library* m_library;
};

} // namespace cirrus::ui
