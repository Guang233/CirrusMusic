#pragma once

#include "domain/account/session_types.h"

#include <QObject>

#include <optional>

namespace cirrus::application
{

// Application-side lifecycle authority. No HTTP, cookies, provider JSON or QML.
// A future auth use case/gateway must verify identity BEFORE calling authenticate.
class SessionCoordinator final : public QObject
{
    Q_OBJECT
  public:
    explicit SessionCoordinator(QObject* parent = nullptr);

    [[nodiscard]] domain::SessionSnapshot snapshot() const;
    [[nodiscard]] bool accepts(const domain::SessionToken& token) const;
    [[nodiscard]] domain::SessionToken beginAuthentication();
    // Returns the exact promoted token, never a later reentrant snapshot token.
    [[nodiscard]] std::optional<domain::SessionToken>
    authenticate(const domain::SessionToken& attempt, QString verifiedAccountId);
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
