#include "diagnostics.h"

#include "app_settings.h"
#include "logging/log.h"
#include "logging/logger.h"

#include <QDesktopServices>
#include <QLocale>
#include <QQmlEngine>
#include <QQmlError>
#include <QVariantMap>

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
