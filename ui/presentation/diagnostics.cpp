#include "diagnostics.h"

#include "app_settings.h"
#include "logging/log.h"
#include "logging/logger.h"

#include "account.h"
#include "app.h"
#include "cache_status.h"
#include "network_status.h"
#include "notice.h"
#include "session.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMetaProperty>
#include <QNetworkProxyFactory>
#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#endif
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QSysInfo>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace cirrus::ui
{

namespace
{
// Names that identify, never describe (Diagnostics::ids()).
bool identifying(QStringView name)
{
    static const QStringList names = {u"shelf"_s,  u"feed"_s, u"date"_s,    u"tab"_s,
                                      u"target"_s, u"kind"_s, u"category"_s};
    return name.endsWith(u"Key") || names.contains(name);
}

// cirrus-20261111-070345-230368-2.log -> cirrus-20261111-070345-230368
QString sessionOf(const QString& fileName)
{
    static const QRegularExpression name(QStringLiteral(R"(^(cirrus-\d{8}-\d{6}-\d+))"));
    const auto match = name.match(fileName);
    return match.hasMatch() ? match.captured(1) : fileName;
}

// What summary.txt says about a log file.
struct LogScan {
    QString first, last; // the first and last record's time
    bool ended = false;  // app.exit seen
    int marks = 0;
    QStringList markTimes;
    QString signal;               // "fatal signal 11"
    QHash<QString, int> warnings; // "cirrus.net request.failed" -> count
};

LogScan scanLog(const QString& path)
{
    LogScan scan;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return scan;
    static const QRegularExpression record(QStringLiteral(R"(^(\S+) ([DIWCF]) \S+\s+(\S+) (\S+))"));
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u"fatal signal ")) {
            scan.signal = line;
            continue;
        }
        const auto match = record.match(line);
        if (!match.hasMatch())
            continue;
        const QString time = match.captured(1);
        if (scan.first.isEmpty())
            scan.first = time;
        scan.last = time;
        const QString event = match.captured(4);
        if (event == u"app.exit")
            scan.ended = true;
        else if (event == u"user.mark") {
            ++scan.marks;
            scan.markTimes.append(time);
        }
        if (match.captured(2) == u"W" || match.captured(2) == u"C")
            ++scan.warnings[match.captured(3) + u' ' + event];
    }
    return scan;
}

QString proxyName(QNetworkProxy::ProxyType type)
{
    switch (type) {
    case QNetworkProxy::NoProxy:
        return QStringLiteral("none");
    case QNetworkProxy::DefaultProxy:
        return QStringLiteral("default");
    case QNetworkProxy::Socks5Proxy:
        return QStringLiteral("socks5");
    case QNetworkProxy::HttpProxy:
    case QNetworkProxy::HttpCachingProxy:
        return QStringLiteral("http");
    case QNetworkProxy::FtpCachingProxy:
        return QStringLiteral("ftp");
    }
    return QString::number(int(type));
}

// How the app was installed (docs/tasks/release.md): told by where it runs from.
QString installKind()
{
    if (qEnvironmentVariableIsSet("APPIMAGE"))
        return QStringLiteral("appimage");
    if (qEnvironmentVariableIsSet("FLATPAK_ID"))
        return QStringLiteral("flatpak");
    if (qEnvironmentVariableIsSet("SNAP"))
        return QStringLiteral("snap");
    const QString dir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(dir + QStringLiteral("/CMakeCache.txt")))
        return QStringLiteral("development build");
#if defined(Q_OS_WIN)
    return dir.contains(u"Program Files", Qt::CaseInsensitive) ? QStringLiteral("installer")
                                                               : QStringLiteral("portable");
#elif defined(Q_OS_MACOS)
    return dir.contains(u".app/Contents") ? QStringLiteral("app bundle") : QStringLiteral("other");
#else
    if (dir.startsWith(u"/opt/cirrus"))
        return QStringLiteral("deb (/opt/cirrus)");
    if (dir.startsWith(u"/usr/"))
        return QStringLiteral("system package");
    return QStringLiteral("other");
#endif
}

QString graphicsName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("opengl");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("vulkan");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("d3d11");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("d3d12");
    case QSGRendererInterface::Metal:
        return QStringLiteral("metal");
    case QSGRendererInterface::Software:
        return QStringLiteral("software");
    default:
        return QString::number(int(api));
    }
}

void addId(QString& out, QStringView name, const QVariant& value)
{
    if (!identifying(name))
        return;
    QString text;
    if (value.typeId() == QMetaType::QString) {
        text = value.toString();
        if (text.isEmpty())
            return;
        if (name.startsWith(u"user")) // a person's id: hashed like an account
            text = log::account(text).value;
    } else if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong ||
               value.typeId() == QMetaType::Bool) {
        text = value.toString();
    } else {
        return;
    }
    if (!out.isEmpty())
        out += u',';
    out += name.toString() + u':' + text;
}
} // namespace

Diagnostics::Diagnostics(AppSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    m_expiry.setSingleShot(true);
    connect(&m_expiry, &QTimer::timeout, this, [this] {
        qCInfo(lcLog) << log::event("log.verbose_expired");
        applyVerbose();
    });
    if (m_settings)
        connect(m_settings, &AppSettings::verboseLogUntilChanged, this, &Diagnostics::applyVerbose);
    // Still within the 24 hours from before: on again. Off otherwise, unless this run
    // asked for it (--log-level debug).
    if (m_settings && m_settings->verboseLogUntil() > QDateTime::currentDateTime())
        applyVerbose();
    refresh();
}

bool Diagnostics::verbose() const { return log::verbose(); }

void Diagnostics::setVerbose(bool verbose)
{
    if (!m_settings) {
        log::setVerbose(verbose);
        emit verboseChanged();
        return;
    }
    m_settings->setVerboseLogUntil(verbose ? QDateTime::currentDateTime().addMSecs(VerboseMs)
                                           : QDateTime());
    applyVerbose(); // also when the stored value did not change
}

void Diagnostics::applyVerbose()
{
    const QDateTime until = m_settings ? m_settings->verboseLogUntil() : QDateTime();
    const qint64 left = until.isValid() ? QDateTime::currentDateTime().msecsTo(until) : 0;
    const bool on = left > 0;
    m_expiry.stop();
    if (on)
        m_expiry.start(std::chrono::milliseconds(qMin(left, VerboseMs)));
    if (on != log::verbose()) {
        log::setVerbose(on);
        qCInfo(lcLog) << log::event("log.verbose") << log::field("on", on);
    }
    emit verboseChanged();
}

QString Diagnostics::verboseEnds() const
{
    const QDateTime until = m_settings ? m_settings->verboseLogUntil() : QDateTime();
    if (!log::verbose() || !until.isValid() || until <= QDateTime::currentDateTime())
        return {};
    const QLocale locale;
    const QString time = locale.toString(until.time(), QStringLiteral("HH:mm"));
    return until.date() == QDate::currentDate() ? time : tr("明天 %1").arg(time);
}

void Diagnostics::refresh()
{
    const auto list = log::files();
    m_available = !log::directory().isEmpty();
    m_files.clear();
    m_paths.clear();
    m_bytes = 0;
    m_oldBytes = 0;
    QString previous; // the newest earlier session
    for (const auto& f : list) {
        const QString session = sessionOf(f.name);
        if (previous.isEmpty() && !f.current)
            previous = session;
        const bool suggested = f.current || session == previous || [&f] {
            const LogScan scan = scanLog(f.path);
            return scan.marks > 0 || !scan.ended;
        }();
        m_files.append(QVariantMap{{QStringLiteral("name"), f.name},
                                   {QStringLiteral("bytes"), f.bytes},
                                   {QStringLiteral("modified"), f.modified.toMSecsSinceEpoch()},
                                   {QStringLiteral("current"), f.current},
                                   {QStringLiteral("suggested"), suggested}});
        m_paths.append(f.path);
        m_bytes += f.bytes;
        if (!f.current)
            m_oldBytes += f.bytes;
    }
    emit filesChanged();
}

void Diagnostics::openFolder() const
{
    const QString dir = log::directory();
    if (!dir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

bool Diagnostics::exportTo(const QUrl& target, const QStringList& names, const QString& description)
{
    logState("export");
    log::writeContext("export");
    log::flush(); // what was logged up to now is in the files
    refresh();
    QStringList paths;
    for (const QString& name : names) {
        const qsizetype at = [&] {
            for (qsizetype i = 0; i < m_files.size(); ++i)
                if (m_files.at(i).toMap().value(QStringLiteral("name")).toString() == name)
                    return i;
            return qsizetype(-1);
        }();
        if (at < 0)
            return false;
        paths.append(m_paths.at(at));
    }
    QList<log::ArchiveEntry> extras = {{QStringLiteral("summary.txt"), summary(names).toUtf8()}};
    if (!description.trimmed().isEmpty()) // the user's own words, for us: as written
        extras.append({QStringLiteral("description.txt"), description.trimmed().toUtf8() + '\n'});
    const QString file = target.isLocalFile() ? target.toLocalFile() : QString();
    const bool ok = !file.isEmpty() && log::writeArchive(file, paths, extras);
    if (ok)
        qCInfo(lcLog) << log::event("log.exported") << log::field("files", paths.size())
                      << log::field("description", !description.trimmed().isEmpty())
                      << log::field("to", log::path(file));
    else
        qCWarning(lcLog) << log::event("log.export_failed") << log::field("files", paths.size())
                         << log::field("to", log::path(file));
    return ok;
}

QString Diagnostics::packageName()
{
    return QStringLiteral("cirrus-diagnostics-%1.zip")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
}

void Diagnostics::clear()
{
    const qint64 freed = log::clearOld();
    qCInfo(lcLog) << log::event("log.cleared") << log::field("bytes", freed);
    refresh();
}

QString noticed(const char* id, const QString& text)
{
    qCInfo(lcUiNav) << log::event("ui.notice") << log::field("id", id);
    return text;
}

QString Diagnostics::typeName(const QObject* object)
{
    if (!object)
        return QStringLiteral("-");
    if (!object->objectName().isEmpty())
        return object->objectName();
    QString name = QString::fromLatin1(object->metaObject()->className());
    static const QRegularExpression suffix(QStringLiteral("_QML(TYPE)?_\\d+$"));
    name.remove(suffix);
    if (const qsizetype at = name.lastIndexOf(u"::"); at >= 0)
        name = name.mid(at + 2);
    return name;
}

QString Diagnostics::ids(const QVariantMap& properties)
{
    QString out;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        addId(out, it.key(), it.value());
    return out.isEmpty() ? QStringLiteral("-") : out;
}

QString Diagnostics::ids(const QObject* object, const QMetaObject& base)
{
    QString out;
    const QMetaObject* meta = object->metaObject();
    for (int i = base.propertyCount(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        addId(out, QString::fromLatin1(property.name()), property.read(object));
    }
    return out.isEmpty() ? QStringLiteral("-") : out;
}

void Diagnostics::pageShown(QObject* page, int depth, const QVariantMap& properties)
{
    if (!page || depth < 1)
        return;
    const QString name = typeName(page);
    const QString id = ids(properties);
    const QString entry = id == u"-" ? name : name + u':' + id;
    if (m_pages.size() == depth && m_pages.last() == entry)
        return; // the same page again (the stack telling twice)
    const char* via = depth > m_pages.size() ? "push" : depth < m_pages.size() ? "back" : "replace";
    m_pages = m_pages.mid(0, depth - 1);
    while (m_pages.size() < depth - 1)
        m_pages.append(QStringLiteral("?"));
    m_pages.append(entry);
    log::setContext("page", name);
    qCInfo(lcUiNav) << log::event("nav.page") << log::field("page", name) << log::field("ids", id)
                    << log::field("depth", depth) << log::field("via", via);
}

void Diagnostics::playerShown(bool shown)
{
    if (shown == m_player)
        return;
    m_player = shown;
    log::setContext("player", shown ? QStringLiteral("open") : QString());
    qCInfo(lcUiNav) << log::event("player.shown") << log::field("shown", shown);
}

void Diagnostics::notice(const QString& id)
{
    static const QRegularExpression safe(QStringLiteral("^[a-z0-9_.]{1,64}$"));
    qCInfo(lcUiNav) << log::event("ui.notice")
                    << log::field("id", safe.match(id).hasMatch() ? id : QStringLiteral("?"));
}

void Diagnostics::watch(QQuickWindow* window)
{
    m_window = window;
    connect(window, &QQuickWindow::activeFocusItemChanged, this, &Diagnostics::focusMoved);
    // The GPU, once (logging-v2 §5.1): the renderer's name on OpenGL; other graphics
    // APIs only say which API.
    connect(
        window, &QQuickWindow::sceneGraphInitialized, this,
        [this, window] {
            QString gpu = graphicsName(window->rendererInterface()->graphicsApi());
#if QT_CONFIG(opengl)
            if (QOpenGLContext* gl = QOpenGLContext::currentContext()) {
                const auto* renderer =
                    reinterpret_cast<const char*>(gl->functions()->glGetString(GL_RENDERER));
                if (renderer)
                    gpu += u' ' + QString::fromLatin1(renderer);
            }
#endif
            QMetaObject::invokeMethod(this, [this, gpu] {
                m_gpu = gpu;
                qCInfo(lcApp) << log::event("app.gpu") << log::field("gpu", gpu);
            });
        },
        Qt::DirectConnection); // on the render thread, where the context is current
}

void Diagnostics::focusMoved()
{
    // A popup's item has the popup as its QObject parent; dialogs and menus take the
    // focus when they open and give it back when they close.
    QString popup;
    for (QQuickItem* item = m_window ? m_window->activeFocusItem() : nullptr; item;
         item = item->parentItem()) {
        if (item->inherits("QQuickPopupItem")) {
            popup = typeName(item->parent());
            break;
        }
    }
    if (popup == m_popup)
        return;
    if (!m_popup.isEmpty())
        qCInfo(lcUiNav) << log::event("ui.popup") << log::field("name", m_popup)
                        << log::field("open", false);
    if (!popup.isEmpty())
        qCInfo(lcUiNav) << log::event("ui.popup") << log::field("name", popup)
                        << log::field("open", true);
    m_popup = popup;
    log::setContext("popup", popup);
}

void Diagnostics::mark()
{
    ++m_marks;
    qCInfo(lcUiNav) << log::event("user.mark") << log::field("n", m_marks);
    logState("mark");
    log::writeContext("mark");
    emit marked();
}

// ui.state (§4.4): what the user was looking at, without a picture of it.
void Diagnostics::logState(const char* reason) const
{
    const auto fields = [this] {
        QList<log::Text> list;
        list.append(
            log::field("pages", m_pages.isEmpty() ? QStringLiteral("-") : m_pages.join(u'>')));
        list.append(log::field("popup", m_popup.isEmpty() ? QStringLiteral("-") : m_popup));
        list.append(log::field("player", m_player));
        if (m_window) {
            list.append(log::field(
                "window", QStringLiteral("%1x%2").arg(m_window->width()).arg(m_window->height())));
            list.append(log::field("visibility", m_window->visibility()));
            list.append(log::field("active", m_window->isActive()));
            list.append(log::field("focus", typeName(m_window->activeFocusItem())));
            if (const QScreen* screen = m_window->screen()) {
                list.append(log::field("screen", QStringLiteral("%1x%2")
                                                     .arg(screen->size().width())
                                                     .arg(screen->size().height())));
                list.append(log::field("dpr", m_window->devicePixelRatio()));
                list.append(log::field("hz", screen->refreshRate()));
            }
            list.append(log::field("screens", QGuiApplication::screens().size()));
        }
        if (m_settings) {
            list.append(log::field("theme", m_settings->themeMode()));
            list.append(log::field("seed", m_settings->seedSource()));
            list.append(log::field("variant", m_settings->variant()));
            list.append(log::field("font", m_settings->fontSource()));
            list.append(log::field("titlebar", m_settings->titleBar()));
            list.append(log::field("rail", m_settings->railExpanded()));
            list.append(log::field("reduced_motion", m_settings->reducedMotion()));
            list.append(log::field("player_view", m_settings->playerView()));
            list.append(log::field("player_theme", m_settings->playerTheme()));
        }
        QString text;
        for (const auto& t : std::as_const(list))
            text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + t.text;
        return log::Text{text};
    }();
    qCInfo(lcUiNav) << log::event("ui.state") << log::field("reason", reason) << fields;
}

QString Diagnostics::summary(const QStringList& names) const
{
    QString out;
    const auto line = [&out](const QString& key, const QString& value) {
        out += key + QStringLiteral(": ") + value + u'\n';
    };
    const auto section = [&out](const char* title) {
        out += (out.isEmpty() ? QString() : QStringLiteral("\n")) + QStringLiteral("[") +
               QString::fromLatin1(title) + QStringLiteral("]\n");
    };

    section("app");
    line(QStringLiteral("version"), QCoreApplication::applicationVersion());
    line(QStringLiteral("install"), installKind());
    line(QStringLiteral("exported"), QDateTime::currentDateTime().toString(Qt::ISODate));

    section("system");
    line(QStringLiteral("os"), QSysInfo::prettyProductName());
    line(QStringLiteral("kernel"), QSysInfo::kernelVersion());
    line(QStringLiteral("arch"), QSysInfo::currentCpuArchitecture());
    line(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    line(QStringLiteral("desktop"), qEnvironmentVariable("XDG_CURRENT_DESKTOP", u"-"_s));
    line(QStringLiteral("session"), qEnvironmentVariable("XDG_SESSION_TYPE", u"-"_s));
    line(QStringLiteral("qpa"), QGuiApplication::platformName());
    line(QStringLiteral("gpu"), m_gpu.isEmpty() ? QStringLiteral("-") : m_gpu);
    line(QStringLiteral("locale"), QLocale::system().name());
    line(QStringLiteral("utc_offset_min"),
         QString::number(QDateTime::currentDateTime().offsetFromUtc() / 60));
    const auto screens = QGuiApplication::screens();
    for (qsizetype i = 0; i < screens.size(); ++i) {
        const QScreen* screen = screens[i];
        line(QStringLiteral("screen%1").arg(i),
             QStringLiteral("%1x%2 dpr=%3 hz=%4%5")
                 .arg(screen->size().width())
                 .arg(screen->size().height())
                 .arg(screen->devicePixelRatio())
                 .arg(screen->refreshRate())
                 .arg(screen == QGuiApplication::primaryScreen() ? QStringLiteral(" primary")
                                                                 : QString()));
    }
    if (m_window)
        line(QStringLiteral("window"), QStringLiteral("%1x%2 %3")
                                           .arg(m_window->width())
                                           .arg(m_window->height())
                                           .arg(log::nameOf(m_window->visibility())));

    section("network");
    const auto proxies = QNetworkProxyFactory::systemProxyForQuery(
        QNetworkProxyQuery(QUrl(QStringLiteral("https://music.163.com/"))));
    line(QStringLiteral("system_proxy"),
         proxies.isEmpty() ? QStringLiteral("none") : proxyName(proxies.first().type()));
    bool proxyVariables = false;
    for (const char* name :
         {"http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"})
        proxyVariables = proxyVariables || qEnvironmentVariableIsSet(name);
    line(QStringLiteral("proxy_variables"),
         proxyVariables ? QStringLiteral("set") : QStringLiteral("unset"));
    const App* app = App::instance();
    if (app)
        line(QStringLiteral("online"), app->network()->online() ? u"true"_s : u"false"_s);

    if (app) {
        section("account");
        line(QStringLiteral("status"),
             QString::fromLatin1(
                 QMetaEnum::fromType<account::Status>().valueToKey(app->session()->status())));
        section("storage");
        const auto bytes = [](qint64 n) { // negative: not measured yet
            return n < 0 ? QStringLiteral("unknown") : QString::number(n);
        };
        line(QStringLiteral("cache_bytes"), bytes(app->cache()->bytes()));
        line(QStringLiteral("audio_cache_bytes"), bytes(app->cache()->audioBytes()));
    }

    // Every setting; for lists only how many (search history is the user's words).
    if (m_settings) {
        section("settings");
        const QMetaObject* meta = m_settings->metaObject();
        for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
            const QMetaProperty property = meta->property(i);
            const QVariant value = property.read(m_settings);
            QString text;
            if (value.typeId() == QMetaType::QStringList)
                text = QStringLiteral("%1 items").arg(value.toStringList().size());
            else if (value.typeId() == QMetaType::QDateTime)
                text = value.toDateTime().toString(Qt::ISODate);
            else if (value.canConvert<QString>())
                text = value.toString();
            else
                text = QStringLiteral("?");
            line(QString::fromLatin1(property.name()), text.isEmpty() ? u"-"_s : text);
        }
    }

    section("logs");
    for (qsizetype i = 0; i < m_files.size(); ++i) {
        const QVariantMap f = m_files.at(i).toMap();
        const QString name = f.value(QStringLiteral("name")).toString();
        if (!names.contains(name))
            continue;
        const bool current = f.value(QStringLiteral("current")).toBool();
        const LogScan scan = scanLog(m_paths.at(i));
        QString text = QStringLiteral("%1 .. %2").arg(scan.first, scan.last);
        if (current)
            text += QStringLiteral(" (this run)");
        else if (!scan.ended)
            text += QStringLiteral(" (did not end%1)")
                        .arg(scan.signal.isEmpty() ? QString() : u", "_s + scan.signal);
        line(name, text);
        if (!scan.markTimes.isEmpty())
            line(QStringLiteral("  marks"), scan.markTimes.join(u", "));
        QList<std::pair<QString, int>> warnings;
        for (auto it = scan.warnings.cbegin(); it != scan.warnings.cend(); ++it)
            warnings.append({it.key(), it.value()});
        std::sort(warnings.begin(), warnings.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        for (qsizetype w = 0; w < qMin<qsizetype>(10, warnings.size()); ++w)
            out += QStringLiteral("  %1 x %2\n").arg(warnings[w].second).arg(warnings[w].first);
    }
    return out;
}

void Diagnostics::watch(QQmlEngine* engine)
{
    engine->setOutputWarningsToStandardError(false);
    connect(engine, &QQmlEngine::warnings, this, [](const QList<QQmlError>& errors) {
        for (const QQmlError& error : errors) {
            // qrc:/qt/qml/Cirrus/Components/CoverArt.qml -> Cirrus/Components/CoverArt.qml
            QString file = error.url().path();
            if (const qsizetype at = file.indexOf(QStringLiteral("/qt/qml/")); at >= 0)
                file = file.mid(at + 8);
            else
                file =
                    log::path(error.url().isLocalFile() ? error.url().toLocalFile() : file).value;
            const auto fields = log::join(
                {log::field("file", file.isEmpty() ? QStringLiteral("-") : file),
                 log::field("line", error.line()), log::field("msg", error.description())});
            // CoverProvider's text for a cover the backend could not get (cover.failed).
            if (error.description().endsWith(u": cover is unavailable"))
                qCDebug(lcUiQml) << log::event("qml.warning") << fields;
            else if (error.messageType() == QtCriticalMsg || error.messageType() == QtFatalMsg)
                qCCritical(lcUiQml) << log::event("qml.error") << fields;
            else if (error.messageType() == QtWarningMsg)
                qCWarning(lcUiQml) << log::event("qml.warning") << fields;
            else
                qCInfo(lcUiQml) << log::event("qml.message") << fields;
        }
    });
}

} // namespace cirrus::ui
