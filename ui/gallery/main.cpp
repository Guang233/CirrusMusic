#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Cirrus_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_SamplesPlugin)

// Development preview. `--grab <file>` renders one page, saves a screenshot and
// exits, so visual checks also work headless (QT_QPA_PLATFORM=offscreen).
int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Cirrus Gallery"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cirrus UI gallery"));
    parser.addHelpOption();
    const QCommandLineOption pageOption(QStringLiteral("page"), QStringLiteral("Initial page."),
                                        QStringLiteral("name"), QStringLiteral("resources"));
    const QCommandLineOption grabOption(QStringLiteral("grab"),
                                        QStringLiteral("Save a screenshot to <file> and quit."),
                                        QStringLiteral("file"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Window size, e.g. 1280x800."),
                                        QStringLiteral("WxH"), QStringLiteral("1280x800"));
    const QCommandLineOption delayOption(QStringLiteral("delay"),
                                         QStringLiteral("Milliseconds to wait before grabbing."),
                                         QStringLiteral("ms"), QStringLiteral("1000"));
    parser.addOptions({pageOption, grabOption, sizeOption, delayOption});
    parser.process(app);

    const QStringList size = parser.value(sizeOption).split(QLatin1Char('x'));
    const int width = size.value(0).toInt();
    const int height = size.value(1).toInt();
    if (size.size() != 2 || width <= 0 || height <= 0) {
        qCritical("invalid --size, expected WxH");
        return 2;
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setInitialProperties({{QStringLiteral("initialPage"), parser.value(pageOption)},
                                 {QStringLiteral("width"), width},
                                 {QStringLiteral("height"), height}});
    engine.loadFromModule("CirrusGallery", "Gallery");
    if (engine.rootObjects().isEmpty())
        return -1;

    if (parser.isSet(grabOption)) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (!window)
            return -1;
        const QString file = parser.value(grabOption);
        QTimer::singleShot(parser.value(delayOption).toInt(), window, [window, file]() {
            const bool saved = window->grabWindow().save(file);
            if (!saved)
                qCritical("failed to save screenshot to %s", qPrintable(file));
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }

    return QGuiApplication::exec();
}
