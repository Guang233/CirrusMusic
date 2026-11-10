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

// Phone/email sign-in beside the QR code (App.session.formLogin).
class FormLogin : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.session.formLogin")
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int smsState READ smsState NOTIFY changed) // Account.SmsState
    Q_PROPERTY(QString smsError READ smsError NOTIFY changed)
    // When a code may be sent again, in Date.now() milliseconds; 0 when it may now.
    Q_PROPERTY(double resendAt READ resendAt NOTIFY changed)

  public:
    using QObject::QObject;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] int smsState() const { return m_smsState; }
    [[nodiscard]] QString smsError() const { return m_smsError; }
    [[nodiscard]] double resendAt() const { return double(m_resendAt); }

    void update(const SessionSnapshot& snapshot);

  signals:
    void changed();

  private:
    bool m_busy = false;
    QString m_error;
    int m_smsState = account::SmsIdle;
    QString m_smsError;
    qint64 m_resendAt = 0;
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
    // The signed-in account's key (its user id); empty for guests.
    Q_PROPERTY(QString accountKey READ accountKey NOTIFY changed)
    Q_PROPERTY(cirrus::ui::QrLogin* qrLogin READ qrLogin CONSTANT)
    Q_PROPERTY(cirrus::ui::FormLogin* formLogin READ formLogin CONSTANT)

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
    [[nodiscard]] FormLogin* formLogin() const { return m_formLogin; }
    [[nodiscard]] const SessionSource* source() const { return m_source; }

    Q_INVOKABLE void beginQrLogin();
    Q_INVOKABLE void refreshQr();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void signOut();
    // Phone/email sign-in (Account.FormMethod); remembers like the QR sign-in
    // (qrLogin.remember). `secret` is the SMS code or the password.
    Q_INVOKABLE void beginFormLogin(int method, const QString& countryCode, const QString& user,
                                    const QString& secret);
    Q_INVOKABLE void sendSmsCode(const QString& countryCode, const QString& phone);
    Q_INVOKABLE void clearFormError();

  signals:
    void changed();
    void accountChanged();

  private:
    void update();

    SessionSource* m_source;
    QrLogin* m_qrLogin;
    FormLogin* m_formLogin;
    int m_status = account::Guest;
    QString m_accountKey;
    QString m_nickname;
    QString m_avatarKey;
};

} // namespace cirrus::ui
