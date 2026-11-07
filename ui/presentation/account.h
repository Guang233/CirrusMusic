#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Account state as the session source reports it (docs/ui/presentation.md §5.1).
namespace cirrus::ui::account
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Account)

// Unavailable: the backend failed to start.
enum Status { Starting, Guest, SigningIn, SignedIn, Expired, Unavailable };
Q_ENUM_NS(Status)
enum QrPhase { NoQr, WaitingForScan, WaitingForConfirmation, Confirming, QrExpired, QrFailed };
Q_ENUM_NS(QrPhase)
// Phone/email sign-in beside the QR code (docs/ui/pages/shell.md §4).
enum FormMethod { PhoneCode, PhonePassword, EmailPassword };
Q_ENUM_NS(FormMethod)
enum SmsState { SmsIdle, SmsSending, SmsSent };
Q_ENUM_NS(SmsState)
} // namespace cirrus::ui::account

namespace cirrus::ui
{

struct SessionSnapshot {
    account::Status status = account::Starting;
    // Identifies the signed-in account; empty for guests. A change is an account change.
    QString accountKey;
    QString nickname;
    QString avatarKey; // a cover key, may be empty
    account::QrPhase qrPhase = account::NoQr;
    // The code to scan; QML never sees the login URL behind it.
    QImage qrImage;
    // Bumped with every new code, so the image URL changes.
    int qrSerial = 0;
    // A phone/email sign-in is running (the QR code waits meanwhile).
    bool formBusy = false;
    // Why the last phone/email sign-in failed, translated; empty when none.
    QString formError;
    account::SmsState smsState = account::SmsIdle;
    QString smsError; // why the last SMS code was not sent, translated
    // When a code may be sent again (ms since the epoch); 0 when it may now.
    qint64 smsResendAt = 0;
};

} // namespace cirrus::ui
