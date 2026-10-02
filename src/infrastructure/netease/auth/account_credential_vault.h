#pragma once

#include "infrastructure/netease/api/session_api_client.h"
#include "infrastructure/secrets/secret_store.h"

#include <QPointer>
#include <functional>

namespace cirrus::netease
{

struct SavedCredentialResult {
    secrets::SecretError error = secrets::SecretError::None;
    bool stale = false;
    QByteArray record; // infrastructure only, never log
};

// One writer for the saved account entry. Latest desired state wins, but an
// outstanding write must settle before a logout deletion is dispatched.
// Store and client outlive this object; keep it alive until deletion settles.
class AccountCredentialVault final : public QObject
{
    Q_OBJECT
  public:
    static constexpr char EntryKey[] = "account-session";
    AccountCredentialVault(SessionApiClient& client, secrets::SecretStore& store,
                           QObject* parent = nullptr);
    void read(domain::SessionToken context, QObject* receiver,
              std::function<void(SavedCredentialResult)> done);
    void save(domain::SessionToken context, const QString& accountId);
    void forget(domain::SessionToken context);

  signals:
    void settled(cirrus::domain::SessionToken context, bool remembered,
                 cirrus::secrets::SecretError error);

  private:
    void flush();
    struct Read {
        domain::SessionToken context;
        QPointer<QObject> receiver;
        std::function<void(SavedCredentialResult)> done;
    };
    QPointer<SessionApiClient> m_client;
    secrets::SecretStore& m_store;
    std::optional<Read> m_read;
    QByteArray m_desired;
    domain::SessionToken m_writeContext;
    quint64 m_revision = 0;
    bool m_busy = false;
    bool m_dirty = false;
    bool m_forgotten = false; // ignore old data until a new save succeeds, even after failed deletion
};

} // namespace cirrus::netease
