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
};

} // namespace cirrus::ui
