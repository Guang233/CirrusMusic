#include "update_check.h"

#include "app_settings.h"
#include "logging/categories.h"
#include "logging/log.h"
#include "notice.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace cirrus::ui
{
namespace
{
constexpr qint64 kFirstDelayMs = 30 * 1000;         // after the start: not with its requests
constexpr qint64 kIntervalMs = 24 * 60 * 60 * 1000; // between checks of its own
constexpr int kTimeoutMs = 15 * 1000;

const char* outcomeName(UpdateCheck::Status status)
{
    switch (status) {
    case UpdateCheck::Available:
        return "available";
    case UpdateCheck::UpToDate:
        return "current";
    default:
        return "failed";
    }
}
} // namespace

UpdateCheck::UpdateCheck(AppSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    m_sinceStart.start();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { start(false); });
    connect(m_settings, &AppSettings::updatesAutoChanged, this, &UpdateCheck::schedule);
}

qint64 UpdateCheck::publishedAt() const
{
    return m_release.published.isValid() ? m_release.published.toMSecsSinceEpoch() : 0;
}

qint64 UpdateCheck::lastCheck() const
{
    return std::max<qint64>(0, m_settings->updatesLastCheck());
}

bool UpdateCheck::canCheckAutomatically() const { return m_install != Development; }

void UpdateCheck::setPackage(const QString& package)
{
    if (package == QLatin1String("linux")) {
        // The deb and the AppImage share one build; the AppImage runtime sets APPIMAGE.
        m_install = qEnvironmentVariableIsSet("APPIMAGE") ? AppImage : Deb;
    } else if (package == QLatin1String("windows")) {
        // The installer and the zip share one build; Inno Setup leaves its uninstaller.
        const QDir dir(QCoreApplication::applicationDirPath());
        m_install = QFileInfo::exists(dir.filePath(QStringLiteral("unins000.exe"))) ? WindowsSetup
                                                                                    : WindowsZip;
    } else if (package == QLatin1String("aur")) {
        m_install = Aur;
    } else {
        m_install = Development;
    }
    schedule();
}

void UpdateCheck::schedule()
{
    m_timer.stop();
    if (!canCheckAutomatically() || !m_settings->updatesAuto())
        return;
    // Not before 30 s after the start, nor within 24 h of the last check.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_settings->updatesLastCheck();
    qint64 delay = kFirstDelayMs - m_sinceStart.elapsed();
    if (last > 0 && last <= now)
        delay = std::max(delay, last + kIntervalMs - now);
    m_timer.start(int(std::clamp<qint64>(delay, 0, kIntervalMs)));
}

void UpdateCheck::check() { start(true); }

void UpdateCheck::start(bool manual)
{
    if (m_reply)
        return;
    qCInfo(lcUi) << log::event("update.check")
                 << log::field("trigger",
                               manual ? QStringLiteral("manual") : QStringLiteral("auto"));
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.github.com/repos/Guang233/CirrusMusic/releases/latest")));
    // A plain client name; nothing that identifies the user or this installation.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Cirrus/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(kTimeoutMs);
    m_reply = m_network->get(request);
    setState(Checking, NoFailure);
    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, manual] { finish(reply, manual); });
}

void UpdateCheck::finish(QNetworkReply* reply, bool manual)
{
    reply->deleteLater();
    m_reply.clear();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Status status = Failed;
    Failure failure = NoFailure;
    if (httpStatus == 200) {
        if (const auto release = updates::parseRelease(reply->readAll())) {
            m_release = *release;
            status = updates::isNewer(release->version, QCoreApplication::applicationVersion())
                         ? Available
                         : UpToDate;
        } else {
            failure = Invalid;
        }
    } else if (httpStatus == 404) { // no published release yet
        m_release = {};
        status = UpToDate;
    } else if (httpStatus == 403 || httpStatus == 429) { // GitHub's rate limit
        failure = Limited;
    } else {
        failure = Network;
    }

    qCInfo(lcUi) << log::event("update.result")
                 << log::field("outcome", QString::fromLatin1(outcomeName(status)))
                 << log::field("status", httpStatus)
                 << log::fieldIf(status != Failed && !m_release.version.isEmpty(), "latest",
                                 m_release.version)
                 << log::fieldIf(reply->error() != QNetworkReply::NoError, "err",
                                 log::message(reply->errorString()));

    m_settings->setUpdatesLastCheck(QDateTime::currentMSecsSinceEpoch());
    // A failure is told only for a check by hand; one on its own leaves what was known.
    if (status == Failed && !manual) {
        status = updates::isNewer(m_release.version, QCoreApplication::applicationVersion())
                     ? Available
                     : Idle;
        failure = NoFailure;
    }
    setState(status, failure);
    // Found on its own: told once for each version (the about settings keep showing it).
    if (!manual && status == Available && m_settings->updatesNotified() != m_release.version) {
        m_settings->setUpdatesNotified(m_release.version);
        emit notice(noticed("update.available", tr("Cirrus %1 可以更新").arg(m_release.version)));
    }
    schedule();
}

void UpdateCheck::setState(Status status, Failure failure)
{
    m_status = status;
    m_failure = failure;
    emit changed();
}

bool UpdateCheck::openRelease() const
{
    if (!updates::isReleasePage(m_release.page))
        return false;
    qCInfo(lcUiNav) << log::event("update.open_release");
    return QDesktopServices::openUrl(m_release.page);
}

} // namespace cirrus::ui
