#include "diagnostics.h"

#include "app_settings.h"
#include "logging/log.h"
#include "logging/logger.h"

#include "account.h"
#include "app.h"
#include "cache_status.h"
#include "diagnostic_report.h"
#include "network_status.h"
#include "notice.h"
#include "session.h"

#include <QDesktopServices>
#include <QDir>
#ifdef CIRRUS_HAVE_RHI
#include <rhi/qrhi.h>
#endif
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
        log::info(lcLog, "log.verbose_expired");
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
        log::info(lcLog, "log.verbose", {"on", on});
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
    m_names.clear();
    m_bytes = 0;
    m_oldBytes = 0;
    QString previous; // the newest earlier session
    for (const auto& f : list) {
        const QString session = report::sessionOf(f.name);
        if (previous.isEmpty() && !f.current)
            previous = session;
        const bool suggested = f.current || session == previous || [&f] {
            const report::LogScan scan = report::scanLog(f.path);
            return !scan.marks.isEmpty() || !scan.ended;
        }();
        m_files.append(QVariantMap{{QStringLiteral("name"), f.name},
                                   {QStringLiteral("bytes"), f.bytes},
                                   {QStringLiteral("modified"), f.modified.toMSecsSinceEpoch()},
                                   {QStringLiteral("current"), f.current},
                                   {QStringLiteral("suggested"), suggested}});
        m_paths.append(f.path);
        m_names.append(f.name);
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

report::UiState Diagnostics::uiState() const
{
    return {m_window, m_settings, m_pages, m_popup, m_player, m_graphics, m_started};
}

// The package's own files (logging-v2 §12) but network.txt, which takes a while.
QList<log::ArchiveEntry> Diagnostics::reports() const
{
    const report::UiState ui = uiState();
    return {{u"about.txt"_s, report::about(ui).toUtf8()},
            {u"system.txt"_s, report::system(ui).toUtf8()},
            {u"graphics.txt"_s, report::graphics(ui).toUtf8()},
            {u"audio.txt"_s, report::audio().toUtf8()},
            {u"environment.txt"_s, report::environment().toUtf8()},
            {u"modules.txt"_s, report::modules().toUtf8()},
            {u"state.txt"_s, report::state(ui).toUtf8()},
            {u"settings.txt"_s, report::settings(m_settings).toUtf8()}};
}

QString Diagnostics::preview(const QStringList& names) const
{
    QString text;
    Q_UNUSED(names); // the reports do not depend on the logs chosen
    for (const auto& entry : reports()) {
        text += u"──── "_s + entry.name + u" ────\n"_s + QString::fromUtf8(entry.data) + u'\n';
    }
    text += u"──── network.txt ────\n"_s +
            tr("导出时检查各网易主机的 DNS 和 HTTPS 连接（经代理和直连）") + u'\n';
    return text;
}

void Diagnostics::exportTo(const QUrl& target, const QStringList& names, const QString& description)
{
    if (m_exporting)
        return;
    logState("export");
    log::writeContext("export");
    log::flush(); // what was logged up to now is in the files
    refresh();
    QStringList paths;
    for (const QString& name : names) {
        const qsizetype at = m_names.indexOf(name);
        if (at < 0) {
            emit exportFinished(false);
            return;
        }
        paths.append(m_paths.at(at));
    }
    m_exporting = true;
    emit exportingChanged();
    // The network check first (up to ~7 s), then the zip.
    auto* probe = new NetworkProbe(this);
    connect(probe, &NetworkProbe::finished, this,
            [this, probe, target, names, paths, description](const QString& network) {
                probe->deleteLater();
                QList<log::ArchiveEntry> entries = reports();
                entries.append({u"network.txt"_s, network.toUtf8()});
                const QString text = description.trimmed();
                if (!text.isEmpty()) // the user's own words, for us: as written
                    entries.append({u"description.txt"_s, text.toUtf8() + '\n'});
                QStringList listed;
                for (const auto& entry : std::as_const(entries))
                    listed.append(entry.name);
                // Crash stacks of earlier runs, as the next run wrote them (no paths).
                const QDir crashes(log::directory() + u"/crashes"_s);
                for (const QFileInfo& info : crashes.entryInfoList({u"*.txt"_s}, QDir::Files)) {
                    QFile file(info.filePath());
                    if (file.open(QIODevice::ReadOnly))
                        entries.append({u"crashes/"_s + info.fileName(), file.readAll()});
                }
                if (!crashes.isEmpty())
                    listed.append(u"crashes/"_s);
                listed.append(u"logs/"_s);
                QList<report::LogScan> scans;
                for (const QString& path : paths)
                    scans.append(report::scanLog(path));
                entries.prepend({u"README.txt"_s,
                                 report::readme(listed, scans, names, !text.isEmpty()).toUtf8()});
                const QString file = target.isLocalFile() ? target.toLocalFile() : QString();
                const bool ok = !file.isEmpty() && log::writeArchive(file, paths, entries);
                if (ok)
                    log::info(lcLog, "log.exported",
                              {{"files", paths.size()},
                               {"reports", entries.size()},
                               {"description", !text.isEmpty()},
                               {"to", log::path(file)}});
                else
                    log::warn(lcLog, "log.export_failed",
                              {{"files", paths.size()}, {"to", log::path(file)}});
                m_exporting = false;
                emit exportingChanged();
                emit exportFinished(ok);
            });
    probe->start();
}

QString Diagnostics::packageName()
{
    return QStringLiteral("cirrus-diagnostics-%1.zip")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
}

void Diagnostics::clear()
{
    const qint64 freed = log::clearOld();
    log::info(lcLog, "log.cleared", {"bytes", freed});
    refresh();
}

QString noticed(const char* id, const QString& text)
{
    log::info(lcUiNav, "ui.notice", {"id", id});
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
    log::info(lcUiNav, "nav.page", {{"page", name}, {"ids", id}, {"depth", depth}, {"via", via}});
}

void Diagnostics::playerShown(bool shown)
{
    if (shown == m_player)
        return;
    m_player = shown;
    log::setContext("player", shown ? QStringLiteral("open") : QString());
    log::info(lcUiNav, "player.shown", {"shown", shown});
}

void Diagnostics::notice(const QString& id)
{
    static const QRegularExpression safe(QStringLiteral("^[a-z0-9_.]{1,64}$"));
    log::info(lcUiNav, "ui.notice", {"id", safe.match(id).hasMatch() ? id : QStringLiteral("?")});
}

void Diagnostics::watch(QQuickWindow* window)
{
    m_window = window;
    connect(window, &QQuickWindow::activeFocusItemChanged, this, &Diagnostics::focusMoved);
    // The GPU and driver (graphics.txt), once, on the render thread where the RHI and the
    // OpenGL context are current; app.gpu in the log.
    connect(
        window, &QQuickWindow::sceneGraphInitialized, this,
        [this, window] {
            const QString text = report::graphicsDevice(window);
            QString device, backend;
#ifdef CIRRUS_HAVE_RHI
            if (QRhi* rhi = window->rhi()) {
                device = QString::fromUtf8(rhi->driverInfo().deviceName);
                backend = QString::fromLatin1(rhi->backendName());
            }
#endif
            QMetaObject::invokeMethod(this, [this, text, device, backend] {
                m_graphics = text;
                log::info(lcApp, "app.gpu", {{"backend", backend}, {"device", device}});
            });
        },
        Qt::DirectConnection);
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
        log::info(lcUiNav, "ui.popup", {{"name", m_popup}, {"open", false}});
    if (!popup.isEmpty())
        log::info(lcUiNav, "ui.popup", {{"name", popup}, {"open", true}});
    m_popup = popup;
    log::setContext("popup", popup);
}

void Diagnostics::mark()
{
    ++m_marks;
    log::info(lcUiNav, "user.mark", {"n", m_marks});
    logState("mark");
    log::writeContext("mark");
    emit marked();
}

// ui.state (§4.4): what the user was looking at, without a picture of it.
void Diagnostics::logState(const char* reason) const
{
    log::Fields fields{{"reason", reason},
                       {"pages", m_pages.isEmpty() ? QStringLiteral("-") : m_pages.join(u'>')},
                       {"popup", m_popup.isEmpty() ? QStringLiteral("-") : m_popup},
                       {"player", m_player}};
    if (m_window) {
        fields.add("window", QStringLiteral("%1x%2").arg(m_window->width()).arg(m_window->height()))
            .add("visibility", m_window->visibility())
            .add("active", m_window->isActive())
            .add("focus", typeName(m_window->activeFocusItem()));
        if (const QScreen* screen = m_window->screen())
            fields
                .add("screen", QStringLiteral("%1x%2")
                                   .arg(screen->size().width())
                                   .arg(screen->size().height()))
                .add("dpr", m_window->devicePixelRatio())
                .add("hz", screen->refreshRate());
        fields.add("screens", QGuiApplication::screens().size());
    }
    if (m_settings)
        fields.add("theme", m_settings->themeMode())
            .add("seed", m_settings->seedSource())
            .add("variant", m_settings->variant())
            .add("font", m_settings->fontSource())
            .add("titlebar", m_settings->titleBar())
            .add("rail", m_settings->railExpanded())
            .add("reduced_motion", m_settings->reducedMotion())
            .add("player_view", m_settings->playerView())
            .add("player_theme", m_settings->playerTheme());
    log::info(lcUiNav, "ui.state", fields);
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
            const log::Fields fields{{"file", file.isEmpty() ? QStringLiteral("-") : file},
                                     {"line", error.line()},
                                     {"msg", error.description()}};
            // CoverProvider's text for a cover the backend could not get (cover.failed).
            if (error.description().endsWith(u": cover is unavailable"))
                log::debug(lcUiQml, "qml.warning", fields);
            else if (error.messageType() == QtCriticalMsg || error.messageType() == QtFatalMsg)
                log::critical(lcUiQml, "qml.error", fields);
            else if (error.messageType() == QtWarningMsg)
                log::warn(lcUiQml, "qml.warning", fields);
            else
                log::info(lcUiQml, "qml.message", fields);
        }
    });
}

} // namespace cirrus::ui
