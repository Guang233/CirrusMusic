#pragma once
#include "application/account/session_coordinator.h"
#include "application/ports/together_gateway.h"
#include "infrastructure/netease/api/session_api_client.h"
namespace cirrus::netease
{
struct TogetherWireRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
[[nodiscard]] std::optional<TogetherWireRequest>
togetherRequest(const domain::TogetherRequest& request, QStringView accountId);
[[nodiscard]] domain::TogetherResult togetherResult(const domain::TogetherRequest& request,
                                                    const ApiResult& result);
class TogetherGateway final : public application::ITogetherGateway
{
    Q_OBJECT
  public:
    TogetherGateway(application::SessionCoordinator& session, SessionApiClient& client,
                    QObject* parent = nullptr);
    ~TogetherGateway() override;
    quint64 submit(domain::SessionToken context, QString accountId,
                   domain::TogetherRequest request) override;
    void cancel(quint64 id) override;

  private:
    struct Pending {
        domain::SessionToken context;
        QString account;
        domain::TogetherRequest request;
        quint64 apiId = 0;
        domain::TogetherError forced{};
    };
    bool current(const Pending& p) const;
    void start(quint64 id);
    void cancelWith(quint64 id, domain::TogetherError error);
    void finish(quint64 id, domain::TogetherResult result);
    application::SessionCoordinator& m_session;
    QPointer<SessionApiClient> m_client;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_apiIds;
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
