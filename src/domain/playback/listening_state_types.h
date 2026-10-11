#pragma once

#include "listening_types.h"

#include <QSet>

namespace cirrus::domain
{
inline constexpr qsizetype MaximumSavedQueueEntries = 10000;

inline bool validSavedQueue(const QueueSnapshot& queue)
{
    if (queue.entries.size() > MaximumSavedQueueEntries || queue.order < QueueOrder::Sequential ||
        queue.order > QueueOrder::Shuffle || queue.repeat < QueueRepeat::Off ||
        queue.repeat > QueueRepeat::One)
        return false;
    QSet<QUuid> ids;
    for (const auto& entry : queue.entries) {
        if (entry.id.isNull() || ids.contains(entry.id) || !validMusicId(entry.song.value))
            return false;
        ids.insert(entry.id);
    }
    return queue.active.isNull() || ids.contains(queue.active);
}

// Guest is a distinct installation-local bucket, never assigned to a logged-in account.
inline bool validListeningAccount(const QString& account)
{
    return account.isEmpty() || validMusicId(account);
}
} // namespace cirrus::domain
