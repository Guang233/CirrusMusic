#pragma once
#include "logging/log.h"

#include <QMetaType>
#include <QString>
#include <QUrl>

namespace cirrus::domain
{

// Ephemeral authentication challenge: do not log/cache/share beyond the login UI.
// loginUrl is constructed locally by the provider adapter, never trusted from JSON.
struct QrLoginChallenge {
    QString key;
    QUrl loginUrl;
};

// Authorized is a QR protocol outcome, NOT an authenticated account/session.
// Identity verification and exact-token credential handoff must happen separately.
enum class QrLoginStatus { Expired, WaitingForScan, WaitingForConfirmation, Authorized };
CIRRUS_LOG_ENUM(QrLoginStatus, Expired, WaitingForScan, WaitingForConfirmation, Authorized)

} // namespace cirrus::domain

Q_DECLARE_METATYPE(cirrus::domain::QrLoginChallenge)
Q_DECLARE_METATYPE(cirrus::domain::QrLoginStatus)
