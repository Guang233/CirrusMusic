#include "app.h"
#include "app_icon.h"
#include "app_settings.h"
#include "desktop.h"
#include "fake_sources.h"
#include "gpu_preference.h"
#include "single_instance.h"
#include "title_bar_mode.h"

#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
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
Q_IMPORT_QML_PLUGIN(Cirrus_SamplesPlugin)
Q_IMPORT_QML_PLUGIN(M3ShapesPlugin)

int main(int argc, char* argv[])
{
    // Development placeholders for where settings live, until the app identity is
    // decided (UQ-06). Set first: the GPU choice reads the settings before the
    // application object exists.
    QCoreApplication::setOrganizationName(QStringLiteral("Cirrus"));
    QCoreApplication::setApplicationName(QStringLiteral("CirrusMusic"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CIRRUS_VERSION));
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

    // Fake data until the backend sources exist (UI phase 5).
    cirrus::ui::App::install(cirrus::ui::makeFakeSources());
    auto* presentation = cirrus::ui::App::instance();
    cirrus::ui::Desktop desktop(&instance);

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
