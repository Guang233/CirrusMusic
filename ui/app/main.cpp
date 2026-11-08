#include "app.h"
#include "app_icon.h"
#include "app_settings.h"
#include "backend_host.h"
#include "backend_sources.h"
#include "desktop.h"
#include "gpu_preference.h"
#ifdef CIRRUS_UI_MPRIS
#include "mpris.h"
#endif
#include "single_instance.h"
#include "title_bar_mode.h"

#include <QCommandLineParser>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#ifdef CIRRUS_UI_TRAY
// QSystemTrayIcon needs QApplication (docs/10-linux-integration.md §4).
#include <QApplication>
using Application = QApplication;
#else
#include <QGuiApplication>
using Application = QGuiApplication;
#endif
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Cirrus_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_WindowPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_PresentationPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_ComponentsPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_PlayerPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_SamplesPlugin)
Q_IMPORT_QML_PLUGIN(M3ShapesPlugin)

namespace
{

// The settings lived in ~/.config/Cirrus/CirrusMusic.conf before the app identity was
// decided (UQ-06); they move to the app's own file once.
void moveOldSettings()
{
    const QString now = QSettings().fileName();
    const QString before =
        QSettings(QStringLiteral("Cirrus"), QStringLiteral("CirrusMusic")).fileName();
    if (!QFile::exists(now) && QFile::exists(before))
        QFile::rename(before, now);
}

} // namespace

int main(int argc, char* argv[])
{
    // The app's identity (UQ-06, docs/ui/integration.md §6 D1). Set first: the GPU
    // choice reads the settings before the application object exists.
    QCoreApplication::setOrganizationName(QStringLiteral("Cirrus"));
    QCoreApplication::setApplicationName(QStringLiteral("Cirrus"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CIRRUS_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("com.guang233.Cirrus"));
    moveOldSettings();
    cirrus::ui::gpu::apply(cirrus::ui::AppSettings().preferDiscreteGpu());

    Application app(argc, argv);
    QGuiApplication::setWindowIcon(cirrus::ui::appIcon());

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(cirrus::ui::titlebar::option());
    parser.process(app);

    // A second launch shows the running app's window instead.
    cirrus::ui::SingleInstance instance;
    if (!instance.claim())
        return 0;

    // The backend (UI phase 5, docs/ui/integration.md): closed before the app quits.
    QString unwritable;
    const auto paths = cirrus::ui::BackendHost::preparePaths(&unwritable);
    if (!paths) {
        qCritical("Cannot create %s", qPrintable(unwritable));
        return 1;
    }
    cirrus::ui::BackendHost backend(*paths);

    // The backend's sources, fakes for what they don't cover yet.
    cirrus::ui::App::install(cirrus::ui::makeBackendSources(backend));
    auto* presentation = cirrus::ui::App::instance();
    cirrus::ui::Desktop desktop(&instance);
    desktop.setShutdown(
        [&backend](std::function<void()> done) { backend.shutdown(std::move(done)); });
    backend.start();
#ifdef CIRRUS_UI_MPRIS
    // Media keys and the desktop's media controls (docs/ui/pages/shell.md §7).
    cirrus::ui::Mpris mpris(presentation->player(), &desktop);
#endif

    // --titlebar, then CIRRUS_TITLEBAR, then the user's setting, then the desktop default.
    namespace titlebar = cirrus::ui::titlebar;
    const int savedTitleBar = presentation->settings()->titleBar();
    const titlebar::Mode titleBar = titlebar::requested(parser).value_or(
        savedTitleBar >= 0 ? titlebar::Mode(savedTitleBar) : titlebar::platformDefault());

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.addImageProvider(QStringLiteral("cover"), presentation->makeCoverProvider());
    engine.addImageProvider(QStringLiteral("qr"), presentation->makeQrProvider());
    engine.setInitialProperties({{QStringLiteral("titleBar"), int(titleBar)}});
    engine.loadFromModule("CirrusMusic", "Main");

    // Hidden in the tray, the window gives back its GPU device and scene graph, so a
    // discrete GPU can power down during hours of background playback.
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) {
        window->setPersistentGraphics(false);
        window->setPersistentSceneGraph(false);
    }

    return QGuiApplication::exec();
}
