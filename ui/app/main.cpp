#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Cirrus_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(M3ShapesPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    // Escape hatch when the custom title bar misbehaves on a desktop (docs/ui/README.md
    // §6.1); CIRRUS_SYSTEM_TITLEBAR=1 does the same without touching the command line.
    const QCommandLineOption systemTitleBarOption(
        QStringLiteral("system-titlebar"),
        QStringLiteral("Use the system title bar instead of the custom one."));
    parser.addOption(systemTitleBarOption);
    parser.process(app);
    const bool systemTitleBar = parser.isSet(systemTitleBarOption) ||
                                qEnvironmentVariableIntValue("CIRRUS_SYSTEM_TITLEBAR") == 1;

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setInitialProperties({{QStringLiteral("customTitleBar"), !systemTitleBar}});
    engine.loadFromModule("CirrusMusic", "Main");

    return QGuiApplication::exec();
}
