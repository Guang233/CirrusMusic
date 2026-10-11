#pragma once

#include "comment_write_messages.h"

#include <QHash>
#include <QList>
#include <optional>

namespace cirrus::application::detail
{

// Internal owner-thread bookkeeping, not a service, scheduler or second account authority.
// Facade supplies the live context on each call and retains signal/timer ownership.
class CommentWriteRequests final
{
  public:
    struct Context {
        bool ready = false;
        quint64 generation = 0;
        domain::SessionSnapshot session;
    };

    // Only advances the Facade's shared ID sequence after all admission checks pass.
    [[nodiscard]] std::optional<CommentWriteCommand>
    submit(const Context& current, domain::CommentMutation mutation, quint64& nextId);
    // Removes pending state before the Facade can emit a reentrant cancellation signal.
    [[nodiscard]] std::optional<CommentWriteDelivery> cancel(quint64 id,
                                                             domain::CommentWriteError error);
    // Snapshot of IDs: invalidation preserves the original per-request cancellation ordering.
    [[nodiscard]] QList<quint64> pendingIds() const;
    // Consumes once and restores original provenance before any completion signal is emitted.
    [[nodiscard]] std::optional<CommentWriteDelivery> complete(const Context& current,
                                                               CommentWriteDelivery delivery);

  private:
    static constexpr qsizetype MaximumPending = 8;
    QHash<quint64, CommentWriteCommand> m_pending;
};

} // namespace cirrus::application::detail
