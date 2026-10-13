#include "resource_cache_store.h"
#include "resource_cache_codec.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QPointer>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <cerrno>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cirrus::cache
{
using namespace domain;
namespace
{
constexpr qint64 Tag = 1128485425; // cache file-format tag, NOT a product/release identity
bool safePath(const QString& root, const QString& path, const QString& mainPath)
{
#ifdef Q_OS_LINUX
    if (getuid() != geteuid() || getgid() != getegid() || root.isEmpty() ||
        !QDir::isAbsolutePath(root) || QDir::cleanPath(root) != root ||
        QFileInfo(root).canonicalFilePath() != root ||
        QFileInfo(path).absoluteFilePath() == QFileInfo(mainPath).absoluteFilePath() ||
        (QFileInfo(path).exists() &&
         QFileInfo(path).canonicalFilePath() == QFileInfo(mainPath).canonicalFilePath()))
        return false;
    struct stat info{};
    auto bytes = QFile::encodeName(root);
    if (lstat(bytes.constData(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 0777) != 0700)
        return false;
    auto ancestor = QFileInfo(root).absolutePath();
    while (true) {
        bytes = QFile::encodeName(ancestor);
        if (lstat(bytes.constData(), &info) || !S_ISDIR(info.st_mode) ||
            (info.st_uid != 0 && info.st_uid != geteuid()) || (info.st_mode & 0022))
            return false;
        if (ancestor == QStringLiteral("/"))
            break;
        ancestor = QFileInfo(ancestor).absolutePath();
    }
    for (const auto& suffix : {QString(), QStringLiteral("-journal"), QStringLiteral("-wal"),
                               QStringLiteral("-shm"), QStringLiteral(".lock")}) {
        bytes = QFile::encodeName(path + suffix);
        if (!lstat(bytes.constData(), &info)) {
            if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
                (info.st_mode & 0777) != 0600 || info.st_nlink != 1)
                return false;
        } else if (errno != ENOENT)
            return false;
    }
    bytes = QFile::encodeName(path);
    const int fd = ::open(bytes.constData(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const bool good = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
                      (info.st_mode & 0777) == 0600 && info.st_nlink == 1;
    ::close(fd);
    return good;
#else
    Q_UNUSED(root);
    Q_UNUSED(path);
    Q_UNUSED(mainPath);
    return false;
#endif
}
bool sql(QSqlDatabase& db, const QString& statement)
{
    QSqlQuery q(db);
    return q.exec(statement);
}
std::optional<qint64> integer(const QVariant& value)
{
    if (value.metaType().id() != QMetaType::Int && value.metaType().id() != QMetaType::LongLong)
        return {};
    return value.toLongLong();
}
std::optional<qint64> scalar(QSqlDatabase& db, const QString& statement)
{
    QSqlQuery q(db);
    if (!q.exec(statement) || !q.next())
        return {};
    return integer(q.value(0));
}
bool schema(QSqlDatabase& db)
{
    const auto version = scalar(db, QStringLiteral("PRAGMA user_version"));
    const auto tag = scalar(db, QStringLiteral("PRAGMA application_id"));
    if (!version || !tag)
        return false;
    if (*version == 1 && *tag == Tag)
        return true;
    if (*version != 0 || *tag != 0 ||
        scalar(db, QStringLiteral("SELECT COUNT(*) FROM sqlite_master")) != 0 || !db.transaction())
        return false;
    const bool ok =
        sql(db, QStringLiteral("CREATE TABLE resources(scope TEXT NOT NULL, kind INTEGER NOT NULL "
                               "CHECK(kind BETWEEN 0 AND 2), "
                               "key TEXT NOT NULL, written INTEGER NOT NULL, expires INTEGER NOT "
                               "NULL, touched INTEGER NOT NULL, "
                               "bytes INTEGER NOT NULL CHECK(bytes>0), digest BLOB NOT NULL, "
                               "payload BLOB NOT NULL, "
                               "PRIMARY KEY(scope,kind,key))")) &&
        sql(db, QStringLiteral("CREATE INDEX resource_lru ON resources(kind,touched)")) &&
        sql(db, QStringLiteral("PRAGMA application_id=1128485425")) &&
        sql(db, QStringLiteral("PRAGMA user_version=1")) && db.commit();
    if (!ok)
        db.rollback();
    return ok;
}
std::optional<CacheUsage> usage(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT COUNT(*),COALESCE(SUM(CASE WHEN kind<2 THEN bytes ELSE 0 END),0),"
            "COALESCE(SUM(CASE WHEN kind=2 THEN bytes ELSE 0 END),0) FROM resources")) ||
        !q.next())
        return {};
    const auto count = integer(q.value(0)), meta = integer(q.value(1)), cover = integer(q.value(2));
    if (!count || !meta || !cover || *count < 0 || *count > 10000 || *meta < 0 || *cover < 0 ||
        *meta > MetadataCacheBudget || *cover > CoverCacheBudget)
        return {};
    return CacheUsage{*meta, *cover, *count, 0};
}
void bindKey(QSqlQuery& q, const QString& scope, const CacheJob& job)
{
    q.addBindValue(scope);
    q.addBindValue(static_cast<int>(job.kind));
    q.addBindValue(job.key);
}
CacheResult execute(QSqlDatabase& db, const QString& path, const CacheJob& job)
{
    const auto fail = [&](CacheError error) {
        CacheResult r;
        r.id = job.id;
        r.error = error;
        return r;
    };
    CacheResult result;
    result.id = job.id;
    const auto scope =
        QString::fromLatin1(QCryptographicHash::hash(QByteArrayLiteral("cache-scope-v1:") +
                                                         (job.source.accountId.isEmpty()
                                                              ? QByteArrayLiteral("guest")
                                                              : QByteArrayLiteral("account:") +
                                                                    job.source.accountId.toUtf8()),
                                                     QCryptographicHash::Sha256)
                                .toHex());
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now <= 0 || now > 253402300799999LL - 7 * CacheDayMs)
        return fail(CacheError::Storage);
    if (job.operation == CacheOperation::Usage) {
        auto value = usage(db);
        if (!value)
            return fail(CacheError::Corrupt);
        value->databaseBytes = QFileInfo(path).size();
        result.usage = *value;
        result.status = CacheStatus::Statistics;
        return result;
    }
    if (job.operation == CacheOperation::Clear || job.operation == CacheOperation::ClearAll ||
        job.operation == CacheOperation::Remove) {
        QSqlQuery q(db);
        if (job.operation == CacheOperation::ClearAll)
            q.prepare(QStringLiteral("DELETE FROM resources"));
        else if (job.operation == CacheOperation::Clear) {
            q.prepare(QStringLiteral("DELETE FROM resources WHERE scope=?"));
            q.addBindValue(scope);
        } else {
            q.prepare(QStringLiteral("DELETE FROM resources WHERE scope=? AND kind=? AND key=?"));
            bindKey(q, scope, job);
        }
        if (!q.exec())
            return fail(CacheError::Storage);
        result.status = CacheStatus::Removed;
        return result;
    }
    if (job.operation == CacheOperation::Read || job.operation == CacheOperation::Cover) {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("SELECT "
                           "written,expires,bytes,length(payload),length(digest),typeof(payload),"
                           "typeof(digest) FROM resources WHERE scope=? AND kind=? AND key=?"));
        bindKey(q, scope, job);
        if (!q.exec())
            return fail(CacheError::Storage);
        if (!q.next())
            return result;
        const auto written = integer(q.value(0)), expires = integer(q.value(1)),
                   bytes = integer(q.value(2)), actual = integer(q.value(3));
        if (!written || !expires || !bytes || !actual || *written <= 0 ||
            *written > 253402300799999LL || *expires <= *written ||
            *expires - *written > maximumCacheTtl(job.kind) || *bytes <= 0 ||
            *bytes > maximumCachePayload(job.kind) || *bytes != *actual ||
            integer(q.value(4)) != 32 || q.value(5).toString() != QStringLiteral("blob") ||
            q.value(6).toString() != QStringLiteral("blob"))
            return fail(CacheError::Corrupt);
        if (now < *written || now >= *expires) {
            result.status = CacheStatus::Expired;
            return result;
        }
        q.finish();
        q.prepare(QStringLiteral(
            "SELECT payload,digest FROM resources WHERE scope=? AND kind=? AND key=?"));
        bindKey(q, scope, job);
        if (!q.exec() || !q.next())
            return fail(CacheError::Storage);
        auto payload = q.value(0).toByteArray();
        if (payload.size() != *bytes ||
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256) !=
                q.value(1).toByteArray())
            return fail(CacheError::Corrupt);
        auto value = decodeCacheValue(std::move(payload), job.kind);
        if (!value)
            return fail(CacheError::Corrupt);
        if ((job.kind == CacheKind::Song && std::get<Song>(*value).id.value != job.key) ||
            (job.kind == CacheKind::Lyrics && std::get<SongLyrics>(*value).song.value != job.key))
            return fail(CacheError::Corrupt);
        q.finish();
        q.prepare(
            QStringLiteral("UPDATE resources SET touched=? WHERE scope=? AND kind=? AND key=?"));
        q.addBindValue(now);
        bindKey(q, scope, job);
        if (!q.exec())
            return fail(CacheError::Storage);
        result.value = std::move(*value);
        result.status = CacheStatus::Hit;
        result.persisted = true;
        return result;
    }
    if (job.operation != CacheOperation::Write || job.payload.isEmpty() ||
        job.payload.size() > maximumCachePayload(job.kind) || job.ttlMs <= 0 ||
        job.ttlMs > maximumCacheTtl(job.kind) || job.notAfterMs > now + job.ttlMs)
        return fail(CacheError::InvalidRequest);
    const auto expires =
        job.retention
            ? std::min(job.notAfterMs, now + std::min(job.ttlMs, job.retention->remainingMs()))
            : job.notAfterMs;
    if (expires <= now) {
        result.status = CacheStatus::Expired;
        return result;
    }
    if (!db.transaction())
        return fail(CacheError::Storage);
    const auto rollback = [&](CacheError error) {
        db.rollback();
        return fail(error);
    };
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM resources WHERE scope=? AND kind=? AND key=?"));
    bindKey(q, scope, job);
    if (!q.exec())
        return rollback(CacheError::Storage);
    bool room = false;
    for (int victims = 0; victims <= 128; ++victims) {
        const auto value = usage(db);
        if (!value)
            return rollback(CacheError::Corrupt);
        const bool cover = job.kind == CacheKind::Cover;
        const bool bytesFull =
            (cover ? value->coverBytes : value->metadataBytes) + job.payload.size() >
            (cover ? CoverCacheBudget : MetadataCacheBudget);
        if (!bytesFull && value->entries < 10000) {
            room = true;
            break;
        }
        if (victims == 128)
            break;
        q.prepare(QStringLiteral(
            "DELETE FROM resources WHERE rowid IN (SELECT rowid FROM resources WHERE (?=0 OR "
            "(kind=2)=?) "
            "ORDER BY CASE WHEN expires<=? OR written>? THEN 0 ELSE 1 END,touched,rowid LIMIT 1)"));
        q.addBindValue(bytesFull ? 1 : 0);
        q.addBindValue(cover ? 1 : 0);
        q.addBindValue(now);
        q.addBindValue(now);
        if (!q.exec() || q.numRowsAffected() != 1)
            return rollback(CacheError::Capacity);
    }
    if (!room)
        return rollback(CacheError::Capacity);
    q.prepare(QStringLiteral(
        "INSERT INTO resources(scope,kind,key,written,expires,touched,bytes,digest,payload) "
        "VALUES(?,?,?,?,?,?,?,?,?)"));
    bindKey(q, scope, job);
    q.addBindValue(now);
    q.addBindValue(expires);
    q.addBindValue(now);
    q.addBindValue(job.payload.size());
    q.addBindValue(QCryptographicHash::hash(job.payload, QCryptographicHash::Sha256));
    q.addBindValue(job.payload);
    if (!q.exec() || !db.commit())
        return rollback(CacheError::Storage);
    result.status = CacheStatus::Stored;
    result.persisted = true;
    return result;
}
CacheResult run(const QString& root, const QString& mainPath, const CacheJob& job)
{
    CacheResult failure;
    failure.id = job.id;
    failure.error = CacheError::Storage;
    const auto path = root + QStringLiteral("/resource-cache.sqlite");
    if (!safePath(root, path, mainPath))
        return failure;
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0) ||
        !QFile::setPermissions(path + QStringLiteral(".lock"),
                               QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return failure;
    const auto connection =
        QStringLiteral("cirrus-cache-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    CacheResult result = failure;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open() && sql(db, QStringLiteral("PRAGMA busy_timeout=100")) &&
            sql(db, QStringLiteral("PRAGMA foreign_keys=ON")) && schema(db)) {
            QSqlQuery journal(db);
            const bool configured = journal.exec(QStringLiteral("PRAGMA journal_mode=DELETE")) &&
                                    journal.next() &&
                                    journal.value(0).toString() == QStringLiteral("delete");
            journal.finish();
            if (configured && sql(db, QStringLiteral("PRAGMA synchronous=FULL")))
                result = execute(db, path, job);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}
} // namespace
ResourceCacheStore::ResourceCacheStore(storage::DatabaseWorker& database, QString root,
                                       QObject* parent)
    : QObject(parent), m_database(database), m_root(std::move(root))
{
}
void ResourceCacheStore::submit(CacheJob job)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const bool write =
        job.operation == CacheOperation::Write || job.operation == CacheOperation::Remove ||
        job.operation == CacheOperation::Clear || job.operation == CacheOperation::ClearAll;
    const bool keyValid =
        job.kind == CacheKind::Cover
            ? job.key.size() == 64 &&
                  std::all_of(
                      job.key.begin(), job.key.end(),
                      [](QChar c) { return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); })
            : validMusicId(job.key);
    if (!job.id || m_pending >= 8 || job.kind < CacheKind::Song || job.kind > CacheKind::Cover ||
        job.operation < CacheOperation::Read || job.operation > CacheOperation::Cover ||
        (job.source.accountId.size() && !validMusicId(job.source.accountId)) ||
        ((job.operation == CacheOperation::Read || job.operation == CacheOperation::Write ||
          job.operation == CacheOperation::Cover || job.operation == CacheOperation::Remove) &&
         !keyValid)) {
        CacheResult result;
        result.id = job.id;
        result.error = CacheError::InvalidRequest;
        m_writeFailed = m_writeFailed || write;
        emit completed(result);
        return;
    }
    ++m_pending;
    const auto id = job.id;
    m_database.run<CacheResult>(
        this,
        [root = m_root, main = m_database.path(), job = std::move(job)](QSqlDatabase&) {
            return storage::StorageResult<CacheResult>{storage::StorageError::None,
                                                       run(root, main, job)};
        },
        [this, id, write](storage::StorageResult<CacheResult> result) {
            --m_pending;
            if (!result.ok()) {
                result.value = {};
                result.value.id = id;
                result.value.error = CacheError::Storage;
            }
            m_writeFailed = m_writeFailed || (write && result.value.error != CacheError::None);
            const QPointer<ResourceCacheStore> guard(this);
            emit completed(std::move(result.value));
            if (guard && !m_pending)
                emit idle();
        });
}
} // namespace cirrus::cache
