#pragma once

#include "domain/account/session_types.h"

#include <QObject>

#include <optional>

namespace cirrus::application
{

// Application-side lifecycle authority. No HTTP, cookies, provider JSON or QML.
// Verify identity before preparation; publish Authenticated only after the
// provider acknowledges credential binding to the exact prepared token.
class SessionCoordinator final : public QObject
{
    Q_OBJECT
  public:
    explicit SessionCoordinator(QObject* parent = nullptr);

    [[nodiscard]] domain::SessionSnapshot snapshot() const;
    [[nodiscard]] bool accepts(const domain::SessionToken& token) const;
    [[nodiscard]] domain::SessionToken beginAuthentication();
    // Returns the exact prepared token, never a later reentrant snapshot token.
    [[nodiscard]] std::optional<domain::SessionToken>
    prepareAuthentication(const domain::SessionToken& attempt, QString verifiedAccountId);
    [[nodiscard]] bool completeAuthentication(const domain::SessionToken& prepared);
    [[nodiscard]] bool cancelAuthentication(const domain::SessionToken& attempt);
    [[nodiscard]] bool expire(const domain::SessionToken& context);
    void signOut();

  signals:
    // By value: a reentrant transition must not mutate an earlier notification.
    void changed(cirrus::domain::SessionSnapshot snapshot);

  private:
    domain::SessionToken transition(domain::SessionPhase phase, QString accountId = {});
    domain::SessionSnapshot m_snapshot;
};

} // namespace cirrus::application
