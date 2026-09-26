#pragma once

#include <QMetaType>
#include <QString>
#include <QUuid>

namespace cirrus::domain
{

// Correlation/lifetime guard, not a credential or proof of authorization.
struct SessionToken {
    QUuid coordinatorId;
    quint64 epoch = 0;
    [[nodiscard]] bool isValid() const { return !coordinatorId.isNull() && epoch != 0; }
    friend bool operator==(const SessionToken&, const SessionToken&) = default;
};

enum class SessionPhase { Guest, Authenticating, Authenticated, Expired };

struct SessionSnapshot {
    SessionToken token;
    SessionPhase phase = SessionPhase::Guest;
    QString accountId; // opaque provider ID, never converted through floating point
    friend bool operator==(const SessionSnapshot&, const SessionSnapshot&) = default;
};

} // namespace cirrus::domain

Q_DECLARE_METATYPE(cirrus::domain::SessionToken)
Q_DECLARE_METATYPE(cirrus::domain::SessionSnapshot)
