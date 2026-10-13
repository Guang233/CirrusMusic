#pragma once
#include <QDeadlineTimer>
#include <QMetaType>

namespace cirrus::domain
{
// In-process freshness limit only; not identity, entitlement or a cache write permit.
// Default/Forever/expired never grants storage. Not serialized to disk or IPC.
struct CacheRetention {
    QDeadlineTimer deadline{0, Qt::PreciseTimer};
    qint64 remainingMs() const
    {
        return deadline.isForever() ? 0 : qMax<qint64>(0, deadline.remainingTime());
    }
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::CacheRetention)
