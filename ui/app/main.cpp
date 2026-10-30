#include "app.h"
#include "app_settings.h"
#include "fake_sources.h"
#include "title_bar_mode.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
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
    QGuiApplication app(argc, argv);
    // Development placeholders for where settings live, until the app identity is
    // decided (UQ-06).
    QGuiApplication::setOrganizationName(QStringLiteral("Cirrus"));
    QGuiApplication::setApplicationName(QStringLiteral("CirrusMusic"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(cirrus::ui::titlebar::option());
    parser.process(app);

    // Fake data until the backend sources exist (UI phase 5).
    cirrus::ui::App::install(cirrus::ui::makeFakeSources());
    auto* presentation = cirrus::ui::App::instance();

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
    engine.setInitialProperties({{QStringLiteral("titleBar"), int(titleBar)}});
    engine.loadFromModule("CirrusMusic", "Main");

    return QGuiApplication::exec();
}
