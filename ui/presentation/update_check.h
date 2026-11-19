#pragma once

#include "update_release.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace cirrus::ui
{

class AppSettings;

// Whether GitHub has a newer Cirrus (App.updates; docs/tasks/update-check.md). Asks
// GitHub's latest-release API: by hand (check()), and in packaged builds also on its
// own, 30 s after the start and every 24 h after (setting updates/auto), not again
// within 24 h of the last check across restarts. A version found on its own is
// announced once (`notice`); the about settings show the state. It never downloads or
// installs anything: openRelease() opens the release page in the browser.
class UpdateCheck : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.updates")
    Q_PROPERTY(int status READ status NOTIFY changed)   // UpdateCheck.Status
    Q_PROPERTY(int failure READ failure NOTIFY changed) // UpdateCheck.Failure
    Q_PROPERTY(int install READ install CONSTANT)       // UpdateCheck.Install
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    // Milliseconds since the epoch, 0 when unknown (for App.format).
    Q_PROPERTY(qint64 publishedAt READ publishedAt NOTIFY changed)
    Q_PROPERTY(qint64 lastCheck READ lastCheck NOTIFY changed)
    // Whether it checks on its own in this build (a packaged one).
    Q_PROPERTY(bool canCheckAutomatically READ canCheckAutomatically CONSTANT)

  public:
    enum Status { Idle, Checking, UpToDate, Available, Failed };
    Q_ENUM(Status)
    enum Failure { NoFailure, Network, Limited, Invalid };
    Q_ENUM(Failure)
    // How this copy was installed: what to download for an update.
    enum Install { Development, Deb, AppImage, WindowsSetup, WindowsZip, Aur };
    Q_ENUM(Install)

    explicit UpdateCheck(AppSettings* settings, QObject* parent = nullptr);

    [[nodiscard]] int status() const { return m_status; }
    [[nodiscard]] int failure() const { return m_failure; }
    [[nodiscard]] int install() const { return m_install; }
    [[nodiscard]] QString latestVersion() const { return m_release.version; }
    [[nodiscard]] QString title() const { return m_release.title; }
    [[nodiscard]] QString notes() const { return m_release.notes; }
    [[nodiscard]] qint64 publishedAt() const;
    [[nodiscard]] qint64 lastCheck() const;
    [[nodiscard]] bool canCheckAutomatically() const;

    // The build's package ("linux", "windows", "aur"; empty for a development build),
    // from CMake's CIRRUS_PACKAGE; the install kind is refined at run time (an AppImage,
    // the Windows installer). Call once, before the event loop.
    void setPackage(const QString& package);

    Q_INVOKABLE void check();
    // The release page in the browser; false when there is none (or not GitHub's).
    Q_INVOKABLE bool openRelease() const;

  signals:
    void changed();
    // A newer version found on its own, for the first time (a snackbar).
    void notice(const QString& message);

  private:
    void schedule();
    void start(bool manual);
    void finish(QNetworkReply* reply, bool manual);
    void setState(Status status, Failure failure);

    AppSettings* m_settings;
    Install m_install = Development;
    Status m_status = Idle;
    Failure m_failure = NoFailure;
    updates::Release m_release;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QElapsedTimer m_sinceStart;
};

} // namespace cirrus::ui
