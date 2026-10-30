#include "title_bar_mode.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Cirrus_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_WindowPlugin)
Q_IMPORT_QML_PLUGIN(M3ShapesPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(cirrus::ui::titlebar::option());
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setInitialProperties(
        {{QStringLiteral("titleBar"), int(cirrus::ui::titlebar::resolve(parser))}});
    engine.loadFromModule("CirrusMusic", "Main");

    return QGuiApplication::exec();
}
