#include "app.h"

#include "app_settings.h"
#include "cover_provider.h"
#include "fake_controls.h"
#include "formatter.h"
#include "network_status.h"
#include "player.h"

#include <QCoreApplication>
#include <QJSEngine>

namespace cirrus::ui
{

namespace
{
App* g_instance = nullptr;
} // namespace

Sources::Sources() = default;
Sources::Sources(Sources&&) noexcept = default;
Sources& Sources::operator=(Sources&&) noexcept = default;
Sources::~Sources() = default;

App::App(Sources sources)
    : m_sources(std::move(sources)), m_settings(new AppSettings(this)),
      m_format(new Formatter(this)), m_network(new NetworkStatus(this)),
      m_player(new Player(m_sources.playback.get(), this))
{
    if (m_sources.fakeControls)
        m_sources.fakeControls->attach(m_network);
}

App::~App()
{
    if (g_instance == this)
        g_instance = nullptr;
}

void App::install(Sources sources)
{
    Q_ASSERT_X(!g_instance, "App::install", "installed twice");
    g_instance = new App(std::move(sources));
    // Destroyed with the application, after the QML engine in main().
    g_instance->setParent(QCoreApplication::instance());
}

App* App::instance() { return g_instance; }

App* App::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT_X(g_instance, "App::create", "call App::install() before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QQuickAsyncImageProvider* App::makeCoverProvider() { return new CoverProvider(images()); }

} // namespace cirrus::ui
