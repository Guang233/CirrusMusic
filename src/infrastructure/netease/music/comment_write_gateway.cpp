#include "comment_write_gateway.h"
#include "infrastructure/netease/api/comment_write_operation.h"
#include "infrastructure/netease/protocol/comment_v3_policy.h"
#include <QThread>
#include <QTimer>
#include <type_traits>
#include <utility>
namespace cirrus::netease
{
using namespace domain;
std::optional<CommentWriteRequest> commentWriteRequest(const CommentMutation& m)
{
    if (!m.isValid())
        return std::nullopt;
    return std::visit(
        [](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            CommentWriteRequest r{ApiOperation::CommentDelete,
                                  {{QStringLiteral("kind"), int(commentResourceKind(c.target))},
                                   {QStringLiteral("id"), commentTargetId(c.target)}}};
            if constexpr (std::is_same_v<T, CommentReaction>)
                r.operation = c.liked ? ApiOperation::CommentLike : ApiOperation::CommentUnlike;
            else if constexpr (std::is_same_v<T, CommentPublish>)
                r.operation = ApiOperation::CommentPublish;
            else if constexpr (std::is_same_v<T, CommentReply>)
                r.operation = ApiOperation::CommentReply;
            if constexpr (std::is_same_v<T, CommentReply>)
                r.parameters.insert(QStringLiteral("comment"), c.parent.value);
            else if constexpr (!std::is_same_v<T, CommentPublish>)
                r.parameters.insert(QStringLiteral("comment"), c.comment.value);
            if constexpr (std::is_same_v<T, CommentPublish> || std::is_same_v<T, CommentReply>)
                r.parameters.insert(QStringLiteral("content"), c.content);
            return r;
        },
        m.change);
}
CommentWriteResult commentWriteResult(const ApiResult& api)
{
    CommentWriteResult r;
    r.outcome =
        api.submittedToTransport ? CommentWriteOutcome::Unknown : CommentWriteOutcome::NotSent;
    if (api.error == ApiError::None && api.decoded.ok() && api.decoded.businessCode) {
        r.providerCode = api.decoded.businessCode;
        r.outcome = *r.providerCode == 200 ? CommentWriteOutcome::Acknowledged
                                           : CommentWriteOutcome::Rejected;
        r.error =
            *r.providerCode == 200 ? CommentWriteError::None : CommentWriteError::ProviderRejected;
    } else if (api.error == ApiError::Canceled)
        r.error = CommentWriteError::Canceled;
    else if (api.error == ApiError::StaleContext)
        r.error = CommentWriteError::StaleContext;
    else if (api.error == ApiError::Busy)
        r.error = CommentWriteError::Busy;
    else if (api.error == ApiError::InvalidRequest)
        r.error = CommentWriteError::InvalidRequest;
    else if (api.error == ApiError::Cookie)
        r.error = CommentWriteError::AuthenticationRequired;
    else if (api.error == ApiError::Decode || api.error == ApiError::None)
        r.error = CommentWriteError::InvalidResponse;
    else if (api.transportError == TransportError::Timeout)
        r.error = CommentWriteError::Timeout;
    else if (api.error == ApiError::Transport)
        r.error = CommentWriteError::Network;
    else
        r.error = CommentWriteError::ProtocolFailure;
    if (api.httpStatus == 429 || api.decoded.businessCode == 429) {
        r.error = CommentWriteError::RateLimited;
        bool ok = false;
        const auto seconds = api.retryAfter.toLongLong(&ok);
        if (ok && seconds >= 0 && seconds <= 86400)
            r.retryAfterMs = seconds * 1000;
    }
    return r;
}
CommentWriteGateway::CommentWriteGateway(application::SessionCoordinator& session,
                                         SessionApiClient& client,
                                         std::unique_ptr<CommentV3Issuer> issuer, QObject* parent)
    : ICommentWriteGateway(parent), m_session(session), m_client(&client),
      m_issuer(std::move(issuer))
{
    if (!m_issuer)
        m_issuer = std::make_unique<CommentV3Issuer>();
    Q_ASSERT(session.thread() == thread() && client.thread() == thread() &&
             m_issuer->thread() == thread());
    connect(&client, &SessionApiClient::completed, this,
            [this](quint64 apiId, SessionToken token, const ApiResult& api) {
                const auto id = m_apiIds.take(apiId);
                if (!m_pending.contains(id))
                    return;
                auto& p = m_pending[id];
                p.apiId = 0;
                auto r = commentWriteResult(api);
                if (!current(p) || p.context != token || p.canceled)
                    r = {{},
                         api.submittedToTransport ? CommentWriteOutcome::Unknown
                                                  : CommentWriteOutcome::NotSent,
                         p.canceled ? CommentWriteError::Canceled
                                    : CommentWriteError::StaleContext};
                finish(id, r);
            });
    connect(m_issuer.get(), &CommentV3Issuer::completed, this,
            [this](quint64 tokenId, CommentTokenResult r) {
                const auto id = m_tokenIds.take(tokenId);
                if (!m_pending.contains(id))
                    return;
                auto& p = m_pending[id];
                p.tokenId = 0;
                if (!current(p) || p.canceled)
                    finish(id, {{},
                                CommentWriteOutcome::NotSent,
                                p.canceled ? CommentWriteError::Canceled
                                           : CommentWriteError::StaleContext});
                else if (r.error != CommentTokenError::None || !validCommentV3Token(r.token))
                    finish(id,
                           {{}, CommentWriteOutcome::NotSent, CommentWriteError::TokenUnavailable});
                else
                    write(id, std::move(r.token));
                r.token.fill('\0');
            });
    connect(&session, &application::SessionCoordinator::changed, this, [this](SessionSnapshot) {
        for (const auto id : m_pending.keys())
            if (!current(m_pending[id]))
                cancel(id);
    });
    connect(&client, &QObject::destroyed, this, [this] {
        for (const auto id : m_pending.keys())
            finish(id, {{},
                        m_pending[id].apiId ? CommentWriteOutcome::Unknown
                                            : CommentWriteOutcome::NotSent,
                        CommentWriteError::Network});
    });
}
CommentWriteGateway::~CommentWriteGateway()
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        for (const auto id : m_apiIds.keys())
            m_client->cancel(id);
    }
}
quint64 CommentWriteGateway::submit(SessionToken context, QString account, CommentMutation mutation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 8)
        return 0;
    for (const auto& p : std::as_const(m_pending))
        if (!mutation.eventId.isNull() && p.mutation.eventId == mutation.eventId)
            return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, std::move(account), std::move(mutation)});
    QTimer::singleShot(0, this, [this, id] { start(id); });
    return id;
}
bool CommentWriteGateway::current(const Pending& p) const
{
    const auto s = m_session.snapshot();
    return m_client && m_session.accepts(p.context) && s.phase == SessionPhase::Authenticated &&
           p.account == s.accountId;
}
void CommentWriteGateway::start(quint64 id)
{
    if (!m_pending.contains(id))
        return;
    auto& p = m_pending[id];
    CommentWriteError error{};
    if (!m_session.accepts(p.context))
        error = CommentWriteError::StaleContext;
    else if (m_session.snapshot().phase != SessionPhase::Authenticated)
        error = CommentWriteError::AuthenticationRequired;
    else if (!validMusicId(p.account) || p.account != m_session.snapshot().accountId ||
             !p.mutation.isValid())
        error = CommentWriteError::InvalidRequest;
    else if (!m_client)
        error = CommentWriteError::Network;
    else if (p.canceled)
        error = CommentWriteError::Canceled;
    if (error != CommentWriteError::None) {
        finish(id, {{}, CommentWriteOutcome::NotSent, error});
        return;
    }
    p.request = commentWriteRequest(p.mutation);
    p.step = requiresCommentV3(p.request->operation) ? CommentWriteStep::Token
                                                     : CommentWriteStep::Writing;
    const auto prerequisite = m_client->commentPrerequisite(p.context, p.request->operation);
    if (prerequisite != ApiError::None) {
        finish(id, commentWriteResult({prerequisite}));
        return;
    }
    if (p.step == CommentWriteStep::Token) {
        p.tokenId = m_issuer->acquire();
        if (!p.tokenId)
            finish(id, {{}, CommentWriteOutcome::NotSent, CommentWriteError::Busy});
        else
            m_tokenIds.insert(p.tokenId, id);
    } else
        write(id);
}
void CommentWriteGateway::write(quint64 id, QByteArray token)
{
    auto& p = m_pending[id];
    if (!current(p) || p.canceled) {
        token.fill('\0');
        finish(id, {{},
                    CommentWriteOutcome::NotSent,
                    p.canceled ? CommentWriteError::Canceled : CommentWriteError::StaleContext});
        return;
    }
    p.step = CommentWriteStep::Writing;
    p.apiId =
        m_client->submit(p.context, p.request->operation, p.request->parameters, std::move(token));
    m_apiIds.insert(p.apiId, id);
    p.request->parameters = {};
}
void CommentWriteGateway::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_pending.contains(id))
        return;
    auto& p = m_pending[id];
    p.canceled = true;
    if (p.apiId && m_client)
        m_client->cancel(p.apiId);
    else if (p.tokenId)
        m_issuer->cancel(p.tokenId);
    else
        QTimer::singleShot(0, this, [this, id] {
            finish(id, {{}, CommentWriteOutcome::NotSent, CommentWriteError::Canceled});
        });
}
void CommentWriteGateway::finish(quint64 id, CommentWriteResult r)
{
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.take(id);
    m_apiIds.remove(p.apiId);
    m_tokenIds.remove(p.tokenId);
    if (p.tokenId)
        m_issuer->cancel(p.tokenId);
    r.eventId = p.mutation.eventId;
    r.step = p.step;
    emit completed(id, p.context, r);
}
} // namespace cirrus::netease
