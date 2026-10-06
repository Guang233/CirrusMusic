#pragma once

#include "application/account/session_coordinator.h"
#include "application/ports/music_gateway.h"

#include <QHash>
#include <QPointer>

namespace cirrus::application
{
class MusicQueryService final : public QObject
{
    Q_OBJECT
  public:
    MusicQueryService(SessionCoordinator& session, IMusicGateway& gateway,
                      QObject* parent = nullptr);
    ~MusicQueryService() override;
    // Caller supplies its observed token; queued requests are never retagged to a newer session.
    [[nodiscard]] quint64 request(domain::SessionToken context,
                                  MusicQuery query); // 0 = capacity exhausted
    void cancel(quint64 id);
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::application::MusicResult result);

  private:
    struct Pending {
        domain::SessionToken context;
        quint64 gatewayId = 0;
        std::optional<MusicError> forced;
        std::optional<QString> authenticatedAccount; // private read's original verified scope
    };
    void rejectLater(quint64 id, MusicError error);
    void finish(quint64 id, MusicResult result);
    SessionCoordinator& m_session;
    QPointer<IMusicGateway> m_gateway;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_ids;
    quint64 m_nextId = 0;
};
} // namespace cirrus::application
