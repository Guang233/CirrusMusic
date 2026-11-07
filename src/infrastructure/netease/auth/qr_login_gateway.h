#pragma once

#include "application/ports/qr_login_gateway.h"
#include "infrastructure/netease/api/session_api_client.h"

#include <QPointer>

namespace cirrus::netease
{
class AccountCredentialVault;

// Worker-side adapter over a shared session client, in the same owning thread.
// One remote QR operation at a time; one issued challenge per session token.
// Renewal/canceling a login attempt requires coordinator invalidation + advanceTo.
class QrLoginGateway final : public application::IQrLoginGateway
{
    Q_OBJECT
  public:
    explicit QrLoginGateway(SessionApiClient& client, QObject* parent = nullptr);
    QrLoginGateway(SessionApiClient& client, AccountCredentialVault& vault,
                   QObject* parent = nullptr);
    ~QrLoginGateway() override;
    [[nodiscard]] quint64 requestKey(domain::SessionToken context) override;
    [[nodiscard]] quint64 checkStatus(domain::SessionToken context, QString key) override;
    [[nodiscard]] bool synchronize(domain::SessionToken context) override;
    [[nodiscard]] quint64 verifyIdentity(domain::SessionToken context) override;
    [[nodiscard]] quint64 promoteCredentials(domain::SessionToken attempt,
                                             domain::SessionToken target, QUuid receipt) override;
    [[nodiscard]] quint64 restoreIdentity(domain::SessionToken context) override;
    void storeSession(domain::SessionToken context, domain::AccountIdentity identity,
                      bool remember) override;
    void forgetSession(domain::SessionToken context) override;
    void cancel(quint64 requestId) override;
    [[nodiscard]] quint64 requestCredentialLogin(domain::SessionToken context,
                                                 domain::CredentialLogin login) override;
    [[nodiscard]] quint64 sendSmsCode(domain::SessionToken context,
                                      domain::SmsCodeRequest request) override;

  private:
    struct Pending {
        domain::SessionToken context;
        ApiOperation operation{};
        quint64 apiId = 0;
        std::optional<application::AuthFailure> failure;
        bool promotion = false;
        QString restoredAccount;
    };
    quint64 submit(domain::SessionToken context, ApiOperation operation, QString key = {});
    quint64 submitWith(domain::SessionToken context, ApiOperation operation,
                       QJsonObject parameters);
    void rejectLater(quint64 requestId, application::AuthFailure failure);
    void finishFailure(quint64 requestId, application::AuthFailure failure);
    void receive(quint64 apiId, domain::SessionToken context, const ApiResult& result);

    QPointer<SessionApiClient> m_client;
    QPointer<AccountCredentialVault> m_vault;
    quint64 m_restoreId = 0;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_apiIds;
    domain::SessionToken m_keyContext;
    QString m_key;
    domain::SessionToken m_authorizedContext;
    std::optional<application::VerifiedLogin> m_verified;
    domain::SessionToken m_boundContext;
    QString m_boundAccount;
    quint64 m_promotionId = 0;
    quint64 m_nextId = 0;
};

} // namespace cirrus::netease
