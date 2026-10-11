#include "listening_state_store.h"
#include "installation_store.h"

#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace cirrus::storage
{
namespace
{
QString uuidText(QUuid id)
{
    return id.isNull() ? QStringLiteral("") : id.toString(QUuid::WithoutBraces);
}
std::optional<QUuid> storedUuid(const QString& text, bool nullable)
{
    if (nullable && text.isEmpty())
        return QUuid{};
    const QUuid id(text);
    return !id.isNull() && uuidText(id) == text ? std::optional<QUuid>(id) : std::nullopt;
}
std::optional<qint64> storedInteger(const QVariant& value)
{
    if (value.metaType().id() != QMetaType::LongLong && value.metaType().id() != QMetaType::Int)
        return std::nullopt;
    return value.toLongLong();
}
StorageError sqlError(const QSqlError& error)
{
    bool ok = false;
    const auto code = error.nativeErrorCode().toInt(&ok);
    return ok && (code & 255) == 13 ? StorageError::InsufficientSpace : StorageError::Query;
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
        const auto order = storedInteger(header.value(1));
        const auto repeat = storedInteger(header.value(2));
        if (!active || !order || *order < 0 || *order > 1 || !repeat || *repeat < 0 || *repeat > 2)
            return fail(StorageError::Corrupt);
        domain::QueueSnapshot queue{{},
                                    *active,
                                    static_cast<domain::QueueOrder>(*order),
                                    static_cast<domain::QueueRepeat>(*repeat)};
        QSqlQuery entries(db);
        entries.prepare(
            QStringLiteral("SELECT ordinal, entry_id, song_id FROM playback_queue_entry "
                           "WHERE account_id = ? ORDER BY ordinal LIMIT 10001"));
        entries.addBindValue(job.accountId);
        if (!entries.exec())
            return fail(StorageError::Query);
        while (entries.next()) {
            const auto id = storedUuid(entries.value(1).toString(), false);
            const auto ordinal = storedInteger(entries.value(0));
            if (!id || !ordinal || *ordinal != queue.entries.size() ||
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
StorageResult<ListeningStateResult> executeHistory(QSqlDatabase& db, const ListeningStateJob& job)
{
    const auto fail = [&](StorageError error) {
        return StorageResult<ListeningStateResult>{error, {job.id, error, {}}};
    };
    if (!job.id || !domain::validListeningAccount(job.accountId))
        return fail(StorageError::Query);
    if (job.operation == ListeningStateOperation::LoadHistory) {
        if (job.pageSize < 1 || job.pageSize > 100 ||
            (job.cursor && (job.cursor->id.isNull() || job.cursor->startedAtMs <= 0 ||
                            job.cursor->startedAtMs > 253402300799999LL)))
            return fail(StorageError::Query);
        QString statement = QStringLiteral(
            "SELECT record_id,song_id,source,entry_id,started_at_ms,ended_at_ms,played_ms,"
            "capped,end_reason,preview_start_ms,preview_end_ms,actual_quality "
            "FROM listening_history WHERE account_id=?");
        if (job.cursor)
            statement +=
                QStringLiteral(" AND (started_at_ms<? OR (started_at_ms=? AND record_id<?))");
        statement += QStringLiteral(" ORDER BY started_at_ms DESC,record_id DESC LIMIT ?");
        QSqlQuery query(db);
        query.prepare(statement);
        query.addBindValue(job.accountId);
        if (job.cursor) {
            query.addBindValue(job.cursor->startedAtMs);
            query.addBindValue(job.cursor->startedAtMs);
            query.addBindValue(uuidText(job.cursor->id));
        }
        query.addBindValue(job.pageSize + 1);
        if (!query.exec())
            return fail(sqlError(query.lastError()));
        domain::ListeningHistoryPage page;
        while (query.next()) {
            domain::StoredListeningRecord row;
            const auto id = storedUuid(query.value(0).toString(), false);
            const auto entry = storedUuid(query.value(3).toString(), true);
            const auto source = storedInteger(query.value(2));
            const auto start = storedInteger(query.value(4));
            const auto played = storedInteger(query.value(6));
            const auto capped = storedInteger(query.value(7));
            const auto end = storedInteger(query.value(8));
            if (!id || !entry || !source || *source < 0 || *source > 1 || !start || !played ||
                !capped || *capped < 0 || *capped > 1 || !end || *end < 0 || *end > 5)
                return fail(StorageError::Corrupt);
            row.id = *id;
            row.accountId = job.accountId;
            row.song = {query.value(1).toString()};
            row.source = static_cast<domain::ListeningMode>(*source);
            row.queueEntry = *entry;
            row.startedAtMs = *start;
            row.playedMs = *played;
            row.capped = *capped != 0;
            row.end = static_cast<domain::ListeningEnd>(*end);
            if (!query.value(5).isNull()) {
                row.endedAtMs = storedInteger(query.value(5));
                if (!row.endedAtMs)
                    return fail(StorageError::Corrupt);
            }
            if (query.value(9).isNull() != query.value(10).isNull())
                return fail(StorageError::Corrupt);
            if (!query.value(9).isNull()) {
                const auto a = storedInteger(query.value(9)), b = storedInteger(query.value(10));
                if (!a || !b)
                    return fail(StorageError::Corrupt);
                row.preview = domain::PreviewRange{*a, *b};
            }
            row.actualQuality = query.value(11).toString();
            if (!domain::validStoredListeningRecord(row))
                return fail(StorageError::Corrupt);
            page.records.append(std::move(row));
        }
        if (page.records.size() > job.pageSize) {
            page.records.removeLast();
            const auto& last = page.records.last();
            page.next = domain::ListeningHistoryCursor{last.startedAtMs, last.id};
        }
        return {StorageError::None, {job.id, StorageError::None, {}, std::move(page)}};
    }
    if (job.operation != ListeningStateOperation::ClearHistory &&
        job.operation != ListeningStateOperation::SaveHistory)
        return fail(StorageError::Query);
    if (job.operation == ListeningStateOperation::SaveHistory &&
        (job.records.isEmpty() || job.records.size() > 64))
        return fail(StorageError::Query);
    for (const auto& row : job.records)
        if (row.accountId != job.accountId || !domain::validStoredListeningRecord(row))
            return fail(StorageError::Query);
    if (!db.transaction())
        return fail(StorageError::Query);
    const auto rollback = [&](StorageError error = StorageError::Query) {
        db.rollback();
        return fail(error);
    };
    QSqlQuery query(db);
    if (job.operation == ListeningStateOperation::ClearHistory) {
        query.prepare(QStringLiteral("DELETE FROM listening_history WHERE account_id=?"));
        query.addBindValue(job.accountId);
        if (!query.exec())
            return rollback(sqlError(query.lastError()));
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO listening_history(account_id,record_id,song_id,source,entry_id,"
            "started_at_ms,ended_at_ms,played_ms,capped,end_reason,preview_start_ms,preview_end_ms,"
            "actual_quality) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(account_id,record_id) "
            "DO UPDATE SET "
            "played_ms=MAX(played_ms,excluded.played_ms),capped=MAX(capped,excluded.capped),"
            "ended_at_ms=COALESCE(ended_at_ms,excluded.ended_at_ms),"
            "end_reason=CASE WHEN end_reason=0 THEN excluded.end_reason ELSE end_reason END,"
            "preview_start_ms=CASE WHEN excluded.played_ms>=played_ms THEN "
            "excluded.preview_start_ms ELSE preview_start_ms END,"
            "preview_end_ms=CASE WHEN excluded.played_ms>=played_ms THEN excluded.preview_end_ms "
            "ELSE preview_end_ms END,"
            "actual_quality=CASE WHEN excluded.played_ms>=played_ms THEN excluded.actual_quality "
            "ELSE actual_quality END "
            "WHERE song_id=excluded.song_id AND source=excluded.source AND "
            "entry_id=excluded.entry_id "
            "AND started_at_ms=excluded.started_at_ms"));
        for (const auto& row : job.records) {
            query.bindValue(0, job.accountId); // canonical non-null empty TEXT for Guest
            query.bindValue(1, uuidText(row.id));
            query.bindValue(2, row.song.value);
            query.bindValue(3, static_cast<int>(row.source));
            query.bindValue(4, uuidText(row.queueEntry));
            query.bindValue(5, row.startedAtMs);
            query.bindValue(6, row.endedAtMs ? QVariant(*row.endedAtMs) : QVariant());
            query.bindValue(7, row.playedMs);
            query.bindValue(8, row.capped ? 1 : 0);
            query.bindValue(9, static_cast<int>(row.end));
            query.bindValue(10, row.preview ? QVariant(row.preview->startMs) : QVariant());
            query.bindValue(11, row.preview ? QVariant(row.preview->endMs) : QVariant());
            query.bindValue(12,
                            row.actualQuality.isEmpty() ? QStringLiteral("") : row.actualQuality);
            if (!query.exec())
                return rollback(sqlError(query.lastError()));
            if (query.numRowsAffected() != 1)
                return rollback(StorageError::Corrupt); // ID reuse cannot overwrite another fact
        }
    }
    if (!db.commit())
        return rollback(sqlError(db.lastError()));
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
    migrations.append(
        {3,
         {QStringLiteral(
              "CREATE TABLE listening_history (account_id TEXT NOT NULL, record_id TEXT NOT NULL, "
              "song_id TEXT NOT NULL, source INTEGER NOT NULL CHECK(source IN (0,1)), entry_id "
              "TEXT NOT NULL, "
              "started_at_ms INTEGER NOT NULL CHECK(typeof(started_at_ms)='integer' AND "
              "started_at_ms>0), "
              "ended_at_ms INTEGER, played_ms INTEGER NOT NULL CHECK(typeof(played_ms)='integer' "
              "AND played_ms BETWEEN 1 AND 604800000), capped INTEGER NOT NULL CHECK(capped IN "
              "(0,1)), "
              "end_reason INTEGER NOT NULL CHECK(end_reason BETWEEN 0 AND 5), "
              "preview_start_ms INTEGER, preview_end_ms INTEGER, actual_quality TEXT NOT NULL, "
              "CHECK((end_reason=0 AND ended_at_ms IS NULL) OR (end_reason<>0 AND ended_at_ms IS "
              "NOT NULL AND ended_at_ms>=started_at_ms)), "
              "PRIMARY KEY(account_id,record_id))"),
          QStringLiteral("CREATE INDEX listening_history_page ON "
                         "listening_history(account_id,started_at_ms DESC,record_id DESC)")}});
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
    if (job.accountId.isEmpty())
        job.accountId = QStringLiteral(""); // empty Guest bucket is TEXT, not driver-dependent NULL
    const bool write = job.operation == ListeningStateOperation::SaveQueue ||
                       job.operation == ListeningStateOperation::SaveHistory ||
                       job.operation == ListeningStateOperation::ClearHistory;
    if (!job.id || m_pending >= 8) {
        m_writeFailed = m_writeFailed || write;
        emit completed({job.id, StorageError::Query, {}});
        return;
    }
    ++m_pending;
    const auto id = job.id;
    m_database.run<ListeningStateResult>(
        this,
        [job = std::move(job)](QSqlDatabase& db) {
            return job.operation == ListeningStateOperation::LoadQueue ||
                           job.operation == ListeningStateOperation::SaveQueue
                       ? executeQueue(db, job)
                       : executeHistory(db, job);
        },
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
