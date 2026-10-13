#pragma once

#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/session_api_client.h"

namespace cirrus::netease
{
class MusicGateway final : public application::IMusicGateway
{
    Q_OBJECT
  public:
    explicit MusicGateway(SessionApiClient& client, QObject* parent = nullptr);
    ~MusicGateway() override;
    quint64 request(domain::SessionToken context, application::MusicQuery query) override;
    void cancel(quint64 id) override;

  private:
    struct Pending {
        domain::SessionToken context;
        application::MusicQuery query;
        quint64 apiId = 0;
        bool canceled = false;
        std::optional<domain::PlaylistSongsPage> page;
    };
    void finish(quint64 id, application::MusicResult result, domain::CacheRetention retention = {});
    void submitApi(quint64 id, ApiOperation operation, QJsonObject parameters);
    void receiveSuccess(quint64 id, const QJsonObject& object, domain::CacheRetention retention);
    QPointer<SessionApiClient> m_client;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_apiIds;
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
