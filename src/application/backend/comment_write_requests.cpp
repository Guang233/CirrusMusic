#include "comment_write_requests.h"

#include "domain/music/music_types.h"

#include <utility>

namespace cirrus::application::detail
{

std::optional<CommentWriteCommand> CommentWriteRequests::submit(const Context& current,
                                                                domain::CommentMutation mutation,
                                                                quint64& nextId)
{
    if (!current.ready || !current.session.token.isValid() ||
        current.session.phase != domain::SessionPhase::Authenticated ||
        !domain::validMusicId(current.session.accountId) || !mutation.isValid() ||
        m_pending.size() >= MaximumPending)
        return std::nullopt;
    for (const auto& pending : std::as_const(m_pending))
        if (pending.mutation.eventId == mutation.eventId)
            return std::nullopt;
    const auto id = ++nextId;
    const CommentWriteCommand command{id, current.generation, current.session.token,
                                      current.session.accountId, std::move(mutation)};
    m_pending.insert(id, command);
    return command;
}

std::optional<CommentWriteDelivery> CommentWriteRequests::cancel(quint64 id,
                                                                 domain::CommentWriteError error)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return std::nullopt;
    const auto command = it.value();
    m_pending.erase(it);
    return CommentWriteDelivery{
        command.id,
        command.generation,
        command.context,
        {command.mutation.eventId, domain::CommentWriteOutcome::Unknown, error}};
}

QList<quint64> CommentWriteRequests::pendingIds() const { return m_pending.keys(); }

std::optional<CommentWriteDelivery> CommentWriteRequests::complete(const Context& current,
                                                                   CommentWriteDelivery delivery)
{
    const auto it = m_pending.find(delivery.id);
    if (it == m_pending.end())
        return std::nullopt;
    const auto command = it.value();
    m_pending.erase(it);
    if (delivery.generation != command.generation || delivery.context != command.context ||
        command.generation != current.generation || command.context != current.session.token ||
        command.accountId != current.session.accountId || !current.ready ||
        current.session.phase != domain::SessionPhase::Authenticated)
        delivery.result = {command.mutation.eventId, domain::CommentWriteOutcome::Unknown,
                           domain::CommentWriteError::StaleContext};
    if (delivery.result.step < domain::CommentWriteStep::Unobserved ||
        delivery.result.step > domain::CommentWriteStep::Writing)
        delivery.result.step = domain::CommentWriteStep::Unobserved;
    if (delivery.result.outcome == domain::CommentWriteOutcome::Acknowledged &&
        delivery.result.step != domain::CommentWriteStep::Writing)
        delivery.result = {command.mutation.eventId, domain::CommentWriteOutcome::Unknown,
                           domain::CommentWriteError::InvalidResponse};
    delivery.result.eventId = command.mutation.eventId;
    return CommentWriteDelivery{command.id, command.generation, command.context,
                                std::move(delivery.result)};
}

} // namespace cirrus::application::detail
