#pragma once

#include "application/ports/qr_login_gateway.h"
#include "infrastructure/netease/api/session_api_client.h"

#include <QPointer>

namespace cirrus::netease
{

// Worker-side adapter over a shared session client, in the same owning thread.
// One remote QR operation at a time; one issued challenge per session token.
// Renewal/canceling a login attempt requires coordinator invalidation + advanceTo.
class QrLoginGateway final : public application::IQrLoginGateway
{
    Q_OBJECT
  public:
    explicit QrLoginGateway(SessionApiClient& client, QObject* parent = nullptr);
    ~QrLoginGateway() override;
    [[nodiscard]] quint64 requestKey(domain::SessionToken context) override;
    [[nodiscard]] quint64 checkStatus(domain::SessionToken context, QString key) override;
    void cancel(quint64 requestId) override;

  private:
    struct Pending {
        domain::SessionToken context;
        ApiOperation operation{};
        quint64 apiId = 0;
        std::optional<application::AuthFailure> failure;
    };
    quint64 submit(domain::SessionToken context, ApiOperation operation, QString key = {});
    void rejectLater(quint64 requestId, application::AuthFailure failure);
    void finishFailure(quint64 requestId, application::AuthFailure failure);
    void receive(quint64 apiId, domain::SessionToken context, const ApiResult& result);

    QPointer<SessionApiClient> m_client;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_apiIds;
    domain::SessionToken m_keyContext;
    QString m_key;
    quint64 m_nextId = 0;
};

} // namespace cirrus::netease
