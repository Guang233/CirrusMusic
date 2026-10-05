#pragma once
#include "application/account/session_coordinator.h"
#include "application/ports/listening_report_gateway.h"
#include "infrastructure/netease/api/session_api_client.h"

namespace cirrus::netease
{
class ListeningReportGateway final : public application::IListeningReportGateway
{
    Q_OBJECT
  public:
    ListeningReportGateway(application::SessionCoordinator& session, SessionApiClient& client,
                           QObject* parent = nullptr);
    ~ListeningReportGateway() override;
    quint64 submit(domain::SessionToken context, QString accountId,
                   domain::ListeningReport report) override;
    void cancel(quint64 id) override;

  private:
    struct Pending {
        domain::SessionToken context;
        QString accountId;
        domain::ListeningReport report;
        quint64 apiId = 0;
    };
    void finish(quint64 id, domain::ListeningReportResult result);
    application::SessionCoordinator& m_session;
    QPointer<SessionApiClient> m_client;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_ids;
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
