#include "resource_cache_store.h"
#include "resource_cache_codec.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QPointer>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <cerrno>
#include <deque>
#include <functional>
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
    if (!count || !meta || !cover || *count < 0 || *count > CacheMaximumEntries || *meta < 0 ||
        *cover < 0 || *meta > MetadataCacheBudget || *cover > CoverCacheBudget)
        return {};
    return CacheUsage{*meta, *cover, *count, 0};
}
void bindKey(QSqlQuery& q, const QString& scope, const CacheJob& job)
{
    q.addBindValue(scope);
    q.addBindValue(static_cast<int>(job.kind));
    q.addBindValue(job.key);
}
QString scopeOf(const CacheJob& job)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            QByteArrayLiteral("cache-scope-v1:") +
                (job.source.accountId.isEmpty()
                     ? QByteArrayLiteral("guest")
                     : QByteArrayLiteral("account:") + job.source.accountId.toUtf8()),
            QCryptographicHash::Sha256)
            .toHex());
}
// A hit whose `touched` is written later, with others, in one transaction.
struct Touch {
    QString scope;
    int kind = 0;
    QString key;
};
CacheResult execute(QSqlDatabase& db, const QString& path, const CacheJob& job,
                    QHash<QString, std::pair<Touch, qint64>>& touches)
{
    const auto fail = [&](CacheError error) {
        CacheResult r;
        r.id = job.id;
        r.error = error;
        return r;
    };
    CacheResult result;
    result.id = job.id;
    const auto scope = scopeOf(job);
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
        const Touch touch{scope, static_cast<int>(job.kind), job.key};
        touches.insert(scope + u'\x1f' + QString::number(touch.kind) + u'\x1f' + job.key,
                       {touch, now});
        result.value = std::move(*value);
        result.status = CacheStatus::Hit;
        result.persisted = true;
        return result;
    }
    return fail(CacheError::InvalidRequest); // writes go through writeOne(), in a batch
}
// One write inside the batch's transaction (its own savepoint is the caller's). `space`
// is the cache's usage, kept up to date here, so a batch counts the rows once.
CacheResult writeOne(QSqlDatabase& db, const CacheJob& job, qint64 now, CacheUsage& space)
{
    const auto fail = [&](CacheError error) {
        CacheResult r;
        r.id = job.id;
        r.error = error;
        return r;
    };
    CacheResult result;
    result.id = job.id;
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
    const auto scope = scopeOf(job);
    const bool cover = job.kind == CacheKind::Cover;
    auto& used = cover ? space.coverBytes : space.metadataBytes;
    QSqlQuery q(db);
    // The row it replaces, if any, leaves the usage first.
    q.prepare(QStringLiteral("SELECT bytes FROM resources WHERE scope=? AND kind=? AND key=?"));
    bindKey(q, scope, job);
    if (!q.exec())
        return fail(CacheError::Storage);
    if (q.next()) {
        const auto old = integer(q.value(0));
        if (!old)
            return fail(CacheError::Corrupt);
        q.finish();
        q.prepare(QStringLiteral("DELETE FROM resources WHERE scope=? AND kind=? AND key=?"));
        bindKey(q, scope, job);
        if (!q.exec())
            return fail(CacheError::Storage);
        used -= *old;
        --space.entries;
    }
    q.finish();
    const qint64 budget = cover ? CoverCacheBudget : MetadataCacheBudget;
    for (int victims = 0;; ++victims) {
        const bool bytesFull = used + job.payload.size() > budget;
        if (!bytesFull && space.entries < CacheMaximumEntries)
            break;
        if (victims == 128)
            return fail(CacheError::Capacity);
        // The expired first, then the least recently used, of the same budget when it is
        // bytes that ran out.
        q.prepare(QStringLiteral(
            "SELECT rowid,bytes,kind FROM resources WHERE (?=0 OR (kind=2)=?) "
            "ORDER BY CASE WHEN expires<=? OR written>? THEN 0 ELSE 1 END,touched,rowid LIMIT 1"));
        q.addBindValue(bytesFull ? 1 : 0);
        q.addBindValue(cover ? 1 : 0);
        q.addBindValue(now);
        q.addBindValue(now);
        if (!q.exec() || !q.next())
            return fail(CacheError::Capacity);
        const auto rowid = integer(q.value(0)), bytes = integer(q.value(1)),
                   kind = integer(q.value(2));
        q.finish();
        if (!rowid || !bytes || !kind)
            return fail(CacheError::Corrupt);
        q.prepare(QStringLiteral("DELETE FROM resources WHERE rowid=?"));
        q.addBindValue(*rowid);
        if (!q.exec() || q.numRowsAffected() != 1)
            return fail(CacheError::Capacity);
        q.finish();
        (*kind == 2 ? space.coverBytes : space.metadataBytes) -= *bytes;
        --space.entries;
    }
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
    if (!q.exec())
        return fail(CacheError::Storage);
    used += job.payload.size();
    ++space.entries;
    result.status = CacheStatus::Stored;
    result.persisted = true;
    return result;
}
} // namespace

// The cache's own thread: one connection, opened (and its path checked) once and kept
// until an error or the end. Reads run as they come; writes, and the `touched` of hits,
// wait a moment and go in together, one transaction (a few fsyncs for the lot).
class StoreConnection : public QObject
{
  public:
    using Deliver = std::function<void(CacheResult)>;
    StoreConnection(QString root, QString mainPath, Deliver deliver)
        : m_root(std::move(root)), m_mainPath(std::move(mainPath)), m_deliver(std::move(deliver)),
          m_flush(this) // a child: it moves to the cache's thread with this
    {
        m_flush.setSingleShot(true);
        connect(&m_flush, &QTimer::timeout, this, &StoreConnection::flush);
    }
    ~StoreConnection() override
    {
        flush();
        close();
    }

    void enqueue(CacheJob job)
    {
        if (job.operation == CacheOperation::Write) {
            m_writes.push_back(std::move(job));
            if (m_writes.size() >= kBatch)
                flush();
            else if (!m_flush.isActive() || m_flush.remainingTime() > kWriteDelayMs)
                m_flush.start(kWriteDelayMs);
            return;
        }
        // In the order asked for: what was written before is there for a read after,
        // and a clear or remove comes after the writes before it.
        const bool reads =
            job.operation == CacheOperation::Read || job.operation == CacheOperation::Cover;
        if (!reads || writing(job))
            flush();
        m_deliver(execute(job));
        if (reads && !m_touches.isEmpty() && !m_flush.isActive())
            m_flush.start(kTouchDelayMs);
    }

  private:
    static constexpr qsizetype kBatch = 32;
    static constexpr int kWriteDelayMs = 50;
    static constexpr int kTouchDelayMs = 2000;

    [[nodiscard]] bool writing(const CacheJob& read) const
    {
        const auto scope = scopeOf(read);
        return std::any_of(m_writes.cbegin(), m_writes.cend(), [&](const CacheJob& write) {
            return write.kind == read.kind && write.key == read.key && scopeOf(write) == scope;
        });
    }

    CacheResult failure(quint64 id, CacheError error) const
    {
        CacheResult result;
        result.id = id;
        result.error = error;
        return result;
    }

    [[nodiscard]] QString path() const { return m_root + QStringLiteral("/resource-cache.sqlite"); }

    bool open()
    {
        if (m_open)
            return true;
        if (!safePath(m_root, path(), m_mainPath))
            return false;
        m_lock = std::make_unique<QLockFile>(path() + QStringLiteral(".lock"));
        m_lock->setStaleLockTime(0);
        if (!m_lock->tryLock(0) ||
            !QFile::setPermissions(path() + QStringLiteral(".lock"),
                                   QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            m_lock.reset();
            return false;
        }
        m_name = QStringLiteral("cirrus-cache-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(path());
        bool ok = db.open() && sql(db, QStringLiteral("PRAGMA busy_timeout=100")) &&
                  sql(db, QStringLiteral("PRAGMA foreign_keys=ON")) && schema(db);
        if (ok) {
            QSqlQuery journal(db);
            ok = journal.exec(QStringLiteral("PRAGMA journal_mode=DELETE")) && journal.next() &&
                 journal.value(0).toString() == QStringLiteral("delete");
            journal.finish();
            ok = ok && sql(db, QStringLiteral("PRAGMA synchronous=FULL"));
        }
        m_open = ok;
        if (!ok)
            close();
        return ok;
    }

    void close()
    {
        if (!m_name.isEmpty()) {
            {
                auto db = QSqlDatabase::database(m_name, false);
                db.close();
            }
            QSqlDatabase::removeDatabase(m_name);
            m_name.clear();
        }
        m_open = false;
        m_lock.reset();
    }

    CacheResult execute(const CacheJob& job)
    {
        if (!open())
            return failure(job.id, CacheError::Storage);
        auto db = QSqlDatabase::database(m_name, false);
        auto result = cirrus::cache::execute(db, path(), job, m_touches);
        // Opened again, and the path checked again, for the next one.
        if (result.error == CacheError::Storage)
            close();
        return result;
    }

    void flush()
    {
        m_flush.stop();
        if (m_writes.empty() && m_touches.isEmpty())
            return;
        auto writes = std::exchange(m_writes, {});
        auto touches = std::exchange(m_touches, {});
        QList<CacheResult> results;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        bool ok = open() && now > 0 && now <= 253402300799999LL - 7 * CacheDayMs;
        auto db = QSqlDatabase::database(m_name, false);
        std::optional<CacheUsage> space;
        ok = ok && db.transaction();
        if (ok && !writes.empty()) {
            space = usage(db);
            ok = space.has_value();
        }
        if (ok) {
            QSqlQuery q(db);
            for (const auto& [touch, at] : std::as_const(touches)) {
                q.prepare(QStringLiteral(
                    "UPDATE resources SET touched=? WHERE scope=? AND kind=? AND key=?"));
                q.addBindValue(at);
                q.addBindValue(touch.scope);
                q.addBindValue(touch.kind);
                q.addBindValue(touch.key);
                if (!q.exec()) {
                    ok = false;
                    break;
                }
            }
        }
        if (ok) {
            for (const CacheJob& job : writes) {
                // Each write keeps or undoes its own changes; the batch commits once.
                if (!sql(db, QStringLiteral("SAVEPOINT cache_write"))) {
                    ok = false;
                    break;
                }
                const auto before = *space;
                auto result = writeOne(db, job, now, *space);
                if (result.error == CacheError::None) {
                    ok = sql(db, QStringLiteral("RELEASE cache_write"));
                } else {
                    *space = before;
                    ok = sql(db, QStringLiteral("ROLLBACK TO cache_write")) &&
                         sql(db, QStringLiteral("RELEASE cache_write"));
                }
                results.append(std::move(result));
                if (!ok)
                    break;
            }
        }
        if (ok)
            ok = db.commit();
        if (!ok) {
            if (db.isOpen())
                db.rollback();
            close();
            results.clear();
            for (const CacheJob& job : writes)
                results.append(failure(job.id, CacheError::Storage));
        }
        for (auto& result : results)
            m_deliver(std::move(result));
    }

    QString m_root, m_mainPath, m_name;
    Deliver m_deliver;
    std::unique_ptr<QLockFile> m_lock;
    bool m_open = false;
    std::deque<CacheJob> m_writes;
    QHash<QString, std::pair<Touch, qint64>> m_touches;
    QTimer m_flush;
};

ResourceCacheStore::ResourceCacheStore(storage::DatabaseWorker& database, QString root,
                                       QObject* parent)
    : QObject(parent), m_root(std::move(root)), m_mainPath(database.path())
{
    m_thread.setObjectName(QStringLiteral("cache"));
    m_connection = new StoreConnection(m_root, m_mainPath, [this](CacheResult result) {
        // On the cache's thread; the store is alive until that thread ended.
        QMetaObject::invokeMethod(
            this, [this, result = std::move(result)]() mutable { delivered(std::move(result)); },
            Qt::QueuedConnection);
    });
    m_connection->moveToThread(&m_thread);
    m_thread.start();
}
ResourceCacheStore::~ResourceCacheStore()
{
    // Pending writes go in before the connection closes, on its own thread.
    QMetaObject::invokeMethod(
        m_connection, [connection = m_connection]() { delete connection; },
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
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
    if (!job.id || m_pending >= CacheJobsInFlight || job.kind < CacheKind::Song ||
        job.kind > CacheKind::Cover || job.operation < CacheOperation::Read ||
        job.operation > CacheOperation::Cover ||
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
    m_writes.insert(job.id, write);
    QMetaObject::invokeMethod(
        m_connection,
        [connection = m_connection, job = std::move(job)]() mutable {
            connection->enqueue(std::move(job));
        },
        Qt::QueuedConnection);
}
void ResourceCacheStore::delivered(CacheResult result)
{
    if (!m_writes.contains(result.id))
        return;
    const bool write = m_writes.take(result.id);
    --m_pending;
    m_writeFailed = m_writeFailed || (write && result.error != CacheError::None);
    const QPointer<ResourceCacheStore> guard(this);
    emit completed(std::move(result));
    if (guard && !m_pending)
        emit idle();
}
} // namespace cirrus::cache
