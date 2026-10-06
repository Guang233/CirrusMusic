#pragma once
#include "application/account/session_coordinator.h"
#include "application/ports/collection_gateway.h"
#include "cover_upload.h"
#include "domain/music/playlist_cover_types.h"
#include "infrastructure/netease/api/session_api_client.h"
namespace cirrus::netease
{
struct CollectionRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
[[nodiscard]] std::optional<CollectionRequest>
collectionRequest(const domain::CollectionMutation& mutation);
[[nodiscard]] domain::CollectionResult collectionResult(const ApiResult& api);
[[nodiscard]] domain::CollectionResult collectionResult(const ApiResult& api,
                                                        const domain::CollectionMutation& mutation);
class CollectionGateway final : public application::ICollectionGateway
{
    Q_OBJECT
  public:
    CollectionGateway(application::SessionCoordinator& session, SessionApiClient& client,
                      QObject* parent = nullptr);
    CollectionGateway(application::SessionCoordinator& session, SessionApiClient& client,
                      std::unique_ptr<CoverUpload> upload, QObject* parent = nullptr);
    ~CollectionGateway() override;
    quint64 submit(domain::SessionToken context, QString accountId,
                   domain::CollectionMutation mutation) override;
    void cancel(quint64 id) override;

  private:
    struct Pending {
        domain::SessionToken context;
        QString accountId;
        domain::CollectionMutation mutation;
        quint64 apiId = 0;
        quint64 uploadId = 0;
        domain::CoverStep step = domain::CoverStep::Preparing;
        QByteArray prepared;
        QString imageId;
        bool anySent = false;
        bool canceled = false;
    };
    bool current(const Pending& p) const;
    void coverCompleted(quint64 id, const ApiResult& api);
    std::unique_ptr<CoverUpload> m_upload;
    QHash<quint64, quint64> m_uploadIds;
    void finish(quint64 id, domain::CollectionResult result);
    application::SessionCoordinator& m_session;
    QPointer<SessionApiClient> m_client;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_ids;
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
