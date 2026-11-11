#include "diagnostics.h"

#include "app_settings.h"
#include "logging/log.h"
#include "logging/logger.h"

#include "notice.h"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QLocale>
#include <QMetaProperty>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace cirrus::ui
{

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
    for (const auto& f : list) {
        m_files.append(QVariantMap{{QStringLiteral("name"), f.name},
                                   {QStringLiteral("bytes"), f.bytes},
                                   {QStringLiteral("modified"), f.modified.toMSecsSinceEpoch()},
                                   {QStringLiteral("current"), f.current}});
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

bool Diagnostics::exportTo(const QUrl& target, const QStringList& names)
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
    const QString file = target.isLocalFile() ? target.toLocalFile() : QString();
    const bool ok = !file.isEmpty() && log::writeArchive(file, paths);
    if (ok)
        qCInfo(lcLog) << log::event("log.exported") << log::field("files", paths.size())
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
