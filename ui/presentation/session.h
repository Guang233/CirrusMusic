#pragma once

#include "account.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class SessionSource;

// QR sign-in in progress (App.session.qrLogin). `qrImage` is an image URL; the code
// itself is drawn in C++, so QML never sees the login URL.
class QrLogin : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.session.qrLogin")
    Q_PROPERTY(int phase READ phase NOTIFY changed) // Account.QrPhase
    Q_PROPERTY(QString qrImage READ qrImage NOTIFY changed)
    // Keep the session across restarts; read when sign-in begins.
    Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY rememberChanged)

  public:
    using QObject::QObject;

    [[nodiscard]] int phase() const { return m_phase; }
    [[nodiscard]] QString qrImage() const;
    [[nodiscard]] bool remember() const { return m_remember; }
    void setRemember(bool remember);

    void update(account::QrPhase phase, int serial);

  signals:
    void changed();
    void rememberChanged();

  private:
    int m_phase = account::NoQr;
    int m_serial = 0;
    bool m_remember = true;
};

// The account (App.session, docs/ui/presentation.md §5.1). Intents return nothing;
// results show in `status`. accountChanged() fires when the signed-in account
// changes, including signing in and out (§4.2).
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.session")
    Q_PROPERTY(int status READ status NOTIFY changed) // Account.Status
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(QString nickname READ nickname NOTIFY changed)
    Q_PROPERTY(QString avatarKey READ avatarKey NOTIFY changed)
    Q_PROPERTY(cirrus::ui::QrLogin* qrLogin READ qrLogin CONSTANT)

  public:
    // Without a source the session stays Guest.
    explicit Session(SessionSource* source, QObject* parent = nullptr);

    [[nodiscard]] int status() const { return m_status; }
    [[nodiscard]] bool signedIn() const { return m_status == account::SignedIn; }
    [[nodiscard]] QString nickname() const { return m_nickname; }
    [[nodiscard]] QString avatarKey() const { return m_avatarKey; }
    // The signed-in account's key (its user id); empty for guests.
    [[nodiscard]] QString accountKey() const { return m_accountKey; }
    [[nodiscard]] QrLogin* qrLogin() const { return m_qrLogin; }
    [[nodiscard]] const SessionSource* source() const { return m_source; }

    Q_INVOKABLE void beginQrLogin();
    Q_INVOKABLE void refreshQr();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void signOut();

  signals:
    void changed();
    void accountChanged();

  private:
    void update();

    SessionSource* m_source;
    QrLogin* m_qrLogin;
    int m_status = account::Guest;
    QString m_accountKey;
    QString m_nickname;
    QString m_avatarKey;
};

} // namespace cirrus::ui
