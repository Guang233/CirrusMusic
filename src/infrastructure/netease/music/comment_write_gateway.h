#pragma once
#include "application/account/session_coordinator.h"
#include "application/ports/comment_write_gateway.h"
#include "comment_v3_issuer.h"
#include "infrastructure/netease/api/session_api_client.h"
namespace cirrus::netease
{
struct CommentWriteRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
[[nodiscard]] std::optional<CommentWriteRequest>
commentWriteRequest(const domain::CommentMutation& mutation);
[[nodiscard]] domain::CommentWriteResult commentWriteResult(const ApiResult& result);
class CommentWriteGateway final : public application::ICommentWriteGateway
{
    Q_OBJECT
  public:
    CommentWriteGateway(application::SessionCoordinator& session, SessionApiClient& client,
                        std::unique_ptr<CommentV3Issuer> issuer = {}, QObject* parent = nullptr);
    ~CommentWriteGateway() override;
    quint64 submit(domain::SessionToken context, QString accountId,
                   domain::CommentMutation mutation) override;
    void cancel(quint64 id) override;

  private:
    struct Pending {
        domain::SessionToken context;
        QString account;
        domain::CommentMutation mutation;
        std::optional<CommentWriteRequest> request;
        domain::CommentWriteStep step = domain::CommentWriteStep::Unobserved;
        quint64 apiId = 0, tokenId = 0;
        bool canceled = false;
    };
    bool current(const Pending& p) const;
    void start(quint64 id);
    void write(quint64 id, QByteArray token = {});
    void finish(quint64 id, domain::CommentWriteResult result);
    application::SessionCoordinator& m_session;
    QPointer<SessionApiClient> m_client;
    std::unique_ptr<CommentV3Issuer> m_issuer;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_apiIds, m_tokenIds;
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
