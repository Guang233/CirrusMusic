#include "listening_state_store.h"
#include "installation_store.h"

#include <QPointer>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace cirrus::storage
{
namespace
{
QString uuidText(QUuid id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
std::optional<QUuid> storedUuid(const QString& text, bool nullable)
{
    if (nullable && text.isEmpty())
        return QUuid{};
    const QUuid id(text);
    return !id.isNull() && uuidText(id) == text ? std::optional<QUuid>(id) : std::nullopt;
}
StorageResult<ListeningStateResult> executeQueue(QSqlDatabase& db, const ListeningStateJob& job)
{
    const auto fail = [&](StorageError error) {
        return StorageResult<ListeningStateResult>{error, {job.id, error, {}}};
    };
    if (!job.id || !domain::validListeningAccount(job.accountId))
        return fail(StorageError::Query);
    if (job.operation == ListeningStateOperation::LoadQueue) {
        QSqlQuery header(db);
        header.prepare(
            QStringLiteral("SELECT active, queue_order, queue_repeat FROM playback_queue "
                           "WHERE account_id = ?"));
        header.addBindValue(job.accountId);
        if (!header.exec())
            return fail(StorageError::Query);
        if (!header.next())
            return {StorageError::None, {job.id, StorageError::None, {}}};
        const auto active = storedUuid(header.value(0).toString(), true);
        const auto order = header.value(1).toLongLong();
        const auto repeat = header.value(2).toLongLong();
        if (!active || order < 0 || order > 1 || repeat < 0 || repeat > 2)
            return fail(StorageError::Corrupt);
        domain::QueueSnapshot queue{{},
                                    *active,
                                    static_cast<domain::QueueOrder>(order),
                                    static_cast<domain::QueueRepeat>(repeat)};
        QSqlQuery entries(db);
        entries.prepare(
            QStringLiteral("SELECT ordinal, entry_id, song_id FROM playback_queue_entry "
                           "WHERE account_id = ? ORDER BY ordinal LIMIT 10001"));
        entries.addBindValue(job.accountId);
        if (!entries.exec())
            return fail(StorageError::Query);
        while (entries.next()) {
            const auto id = storedUuid(entries.value(1).toString(), false);
            if (!id || entries.value(0).toLongLong() != queue.entries.size() ||
                queue.entries.size() == domain::MaximumSavedQueueEntries)
                return fail(StorageError::Corrupt);
            queue.entries.append({*id, {entries.value(2).toString()}});
        }
        if (!domain::validSavedQueue(queue))
            return fail(StorageError::Corrupt);
        return {StorageError::None, {job.id, StorageError::None, std::move(queue)}};
    }
    if (job.operation != ListeningStateOperation::SaveQueue || !domain::validSavedQueue(job.queue))
        return fail(StorageError::Query);
    if (!db.transaction())
        return fail(StorageError::Query);
    const auto rollback = [&] {
        db.rollback();
        return fail(StorageError::Query);
    };
    QSqlQuery erase(db);
    erase.prepare(QStringLiteral("DELETE FROM playback_queue_entry WHERE account_id = ?"));
    erase.addBindValue(job.accountId);
    if (!erase.exec())
        return rollback();
    QSqlQuery header(db);
    header.prepare(
        QStringLiteral("INSERT INTO playback_queue(account_id, active, queue_order, "
                       "queue_repeat) VALUES (?, ?, ?, ?) ON CONFLICT(account_id) DO "
                       "UPDATE SET active=excluded.active, queue_order=excluded.queue_order, "
                       "queue_repeat=excluded.queue_repeat"));
    header.addBindValue(job.accountId);
    header.addBindValue(uuidText(job.queue.active));
    header.addBindValue(static_cast<int>(job.queue.order));
    header.addBindValue(static_cast<int>(job.queue.repeat));
    if (!header.exec())
        return rollback();
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO playback_queue_entry(account_id, ordinal, entry_id, "
                                  "song_id) VALUES (?, ?, ?, ?)"));
    for (qsizetype index = 0; index < job.queue.entries.size(); ++index) {
        const auto& entry = job.queue.entries[index];
        insert.bindValue(0, job.accountId);
        insert.bindValue(1, index);
        insert.bindValue(2, uuidText(entry.id));
        insert.bindValue(3, entry.song.value);
        if (!insert.exec())
            return rollback();
    }
    if (!db.commit())
        return rollback();
    return {StorageError::None, {job.id, StorageError::None, {}}};
}
} // namespace

QList<Migration> ListeningStateStore::migrations()
{
    auto migrations = InstallationStore::migrations();
    migrations.append(
        {2,
         {QStringLiteral(
              "CREATE TABLE playback_queue (account_id TEXT PRIMARY KEY NOT NULL, "
              "active TEXT NOT NULL, queue_order INTEGER NOT NULL CHECK(queue_order IN (0,1)), "
              "queue_repeat INTEGER NOT NULL CHECK(queue_repeat IN (0,1,2)))"),
          QStringLiteral(
              "CREATE TABLE playback_queue_entry (account_id TEXT NOT NULL REFERENCES "
              "playback_queue(account_id) ON DELETE CASCADE, ordinal INTEGER NOT NULL "
              "CHECK(ordinal BETWEEN 0 AND 9999), entry_id TEXT NOT NULL, song_id TEXT NOT NULL, "
              "PRIMARY KEY(account_id, ordinal), UNIQUE(account_id, entry_id))")}});
    return migrations;
}
ListeningStateStore::ListeningStateStore(DatabaseWorker& database, QObject* parent)
    : QObject(parent), m_database(database)
{
    Q_ASSERT(database.thread() == thread());
}
void ListeningStateStore::submit(ListeningStateJob job)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!job.id || m_pending >= 8) {
        m_writeFailed = m_writeFailed || job.operation == ListeningStateOperation::SaveQueue;
        emit completed({job.id, StorageError::Query, {}});
        return;
    }
    ++m_pending;
    const bool write = job.operation == ListeningStateOperation::SaveQueue;
    const auto id = job.id;
    m_database.run<ListeningStateResult>(
        this, [job = std::move(job)](QSqlDatabase& db) { return executeQueue(db, job); },
        [this, id, write](StorageResult<ListeningStateResult> result) {
            --m_pending;
            m_writeFailed = m_writeFailed || (write && !result.ok());
            if (!result.ok())
                result.value = {id, result.error, {}};
            const QPointer<ListeningStateStore> guard(this);
            emit completed(std::move(result.value));
            if (guard && !m_pending)
                emit idle();
        });
}
} // namespace cirrus::storage
