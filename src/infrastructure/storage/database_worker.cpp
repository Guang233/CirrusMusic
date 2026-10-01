#include "database_worker.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QUuid>

#ifdef Q_OS_LINUX
#include <sys/vfs.h>
#endif

#include <optional>

namespace cirrus::storage
{
namespace
{

constexpr QFileDevice::Permissions PrivateFile = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr QFileDevice::Permissions PrivateDir =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;

QString checksum(const Migration& migration)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto& statement : migration.statements) {
        hash.addData(statement.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool exec(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    return query.exec(sql);
}

std::optional<QString> scalar(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql) || !query.next()) {
        return std::nullopt;
    }
    return query.value(0).toString();
}

// WAL needs shared memory on a local filesystem; network/FUSE mounts fall back.
bool walSafe(const QString& directory)
{
#ifdef Q_OS_LINUX
    struct statfs info{};
    if (statfs(QFile::encodeName(directory).constData(), &info) != 0) {
        return false;
    }
    switch (static_cast<unsigned long>(info.f_type)) {
    case 0x6969UL:     // NFS
    case 0x517BUL:     // SMB
    case 0xFF534D42UL: // CIFS
    case 0xFE534D42UL: // SMB2
    case 0x65735546UL: // FUSE (sshfs, rclone...)
    case 0x01021997UL: // 9p (VM shared folders)
        return false;
    default:
        return true;
    }
#else
    Q_UNUSED(directory);
    return true;
#endif
}

} // namespace

class DatabaseWorker::Executor final : public QObject
{
  public:
    Executor(QString path, QList<Migration> migrations)
        : m_path(std::move(path)), m_migrations(std::move(migrations)),
          m_connection(QStringLiteral("cirrus-%1").arg(QUuid::createUuid().toString(QUuid::Id128)))
    {
    }

    StorageError open()
    {
        if (m_db.isOpen()) {
            return StorageError::None;
        }
        const auto error = openConnection();
        if (error != StorageError::None) {
            close();
        }
        return error;
    }

    void run(const Task& task) { task(m_db.isOpen() ? &m_db : nullptr); }

    void close()
    {
        if (m_db.isValid()) {
            m_db.close();
            m_db = {};
            QSqlDatabase::removeDatabase(m_connection);
        }
        if (m_lock) {
            m_lock->unlock();
            m_lock.reset();
        }
    }

  private:
    StorageError openConnection()
    {
        const QFileInfo file(m_path);
        if (m_path.isEmpty() || !file.isAbsolute() || file.fileName().isEmpty()) {
            return StorageError::InvalidPath;
        }
        const auto directory = file.absolutePath();
        if (!QDir().mkpath(directory) || !QFile::setPermissions(directory, PrivateDir)) {
            return StorageError::Unavailable;
        }
        m_lock = std::make_unique<QLockFile>(m_path + QStringLiteral(".lock"));
        m_lock->setStaleLockTime(0); // stale only if the owning process is gone
        if (!m_lock->tryLock(0)) {
            m_lock.reset();
            return StorageError::Locked;
        }
        if (!file.exists()) {
            QFile create(m_path);
            if (!create.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                return StorageError::Unavailable;
            }
        }
        if (!QFile::setPermissions(m_path, PrivateFile)) {
            return StorageError::Unavailable;
        }
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        m_db.setDatabaseName(m_path);
        if (!m_db.open()) {
            return StorageError::Unavailable;
        }
        if (scalar(m_db, QStringLiteral("PRAGMA quick_check")) != QStringLiteral("ok")) {
            return StorageError::Corrupt;
        }
        const auto journal = walSafe(directory) ? QStringLiteral("wal") : QStringLiteral("delete");
        if (!exec(m_db, QStringLiteral("PRAGMA foreign_keys=ON")) ||
            !exec(m_db, QStringLiteral("PRAGMA busy_timeout=3000")) ||
            scalar(m_db, QStringLiteral("PRAGMA journal_mode=%1").arg(journal)) != journal ||
            !exec(m_db, QStringLiteral("PRAGMA synchronous=FULL")) ||
            scalar(m_db, QStringLiteral("PRAGMA foreign_keys")) != QStringLiteral("1")) {
            return StorageError::Unavailable;
        }
        return migrate();
    }

    StorageError migrate()
    {
        for (int i = 0; i < m_migrations.size(); ++i) {
            if (m_migrations[i].version != i + 1 || m_migrations[i].statements.isEmpty()) {
                return StorageError::MigrationFailed; // programming error in the schema list
            }
        }
        if (!exec(m_db, QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                                       "version INTEGER PRIMARY KEY CHECK (version > 0), "
                                       "checksum TEXT NOT NULL, applied_at INTEGER NOT NULL)"))) {
            return StorageError::Unavailable;
        }
        QSqlQuery applied(m_db);
        if (!applied.exec(QStringLiteral(
                "SELECT version, checksum FROM schema_migrations ORDER BY version"))) {
            return StorageError::Corrupt;
        }
        int current = 0;
        while (applied.next()) {
            const int version = applied.value(0).toInt();
            if (version > m_migrations.size()) {
                return StorageError::NewerSchema;
            }
            if (version != current + 1) {
                return StorageError::Corrupt; // gaps mean a damaged or foreign database
            }
            if (applied.value(1).toString() != checksum(m_migrations[version - 1])) {
                return StorageError::ChecksumMismatch;
            }
            current = version;
        }
        applied.finish();
        if (current == m_migrations.size()) {
            return StorageError::None;
        }
        if (current > 0) {
            if (const auto error = backup(current); error != StorageError::None) {
                return error;
            }
        }
        for (int version = current + 1; version <= m_migrations.size(); ++version) {
            if (!apply(m_migrations[version - 1])) {
                return StorageError::MigrationFailed;
            }
        }
        return StorageError::None;
    }

    // Consistent snapshot (works with WAL) before upgrading an existing schema.
    StorageError backup(int version)
    {
        const QFileInfo file(m_path);
        const auto needed = file.size() * 2 + 1024 * 1024;
        if (QStorageInfo(file.absolutePath()).bytesAvailable() < needed) {
            return StorageError::InsufficientSpace;
        }
        const auto target = m_path + QStringLiteral(".backup-v%1").arg(version);
        const auto staging = target + QStringLiteral(".tmp");
        QFile::remove(staging);
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("VACUUM INTO ?"));
        query.addBindValue(staging);
        if (!query.exec() || !QFile::setPermissions(staging, PrivateFile)) {
            QFile::remove(staging);
            return StorageError::BackupFailed;
        }
        QFile::remove(target);
        if (!QFile::rename(staging, target)) {
            QFile::remove(staging);
            return StorageError::BackupFailed;
        }
        return StorageError::None;
    }

    bool apply(const Migration& migration)
    {
        if (!m_db.transaction()) {
            return false;
        }
        bool ok = true;
        for (const auto& statement : migration.statements) {
            ok = ok && exec(m_db, statement);
        }
        if (ok) {
            QSqlQuery record(m_db);
            record.prepare(QStringLiteral(
                "INSERT INTO schema_migrations (version, checksum, applied_at) VALUES (?, ?, ?)"));
            record.addBindValue(migration.version);
            record.addBindValue(checksum(migration));
            record.addBindValue(QDateTime::currentMSecsSinceEpoch());
            QSqlQuery foreignKeys(m_db);
            ok = record.exec() && foreignKeys.exec(QStringLiteral("PRAGMA foreign_key_check")) &&
                 !foreignKeys.next();
        }
        if (ok && m_db.commit()) {
            return true;
        }
        m_db.rollback();
        return false;
    }

    const QString m_path;
    const QList<Migration> m_migrations;
    const QString m_connection;
    QSqlDatabase m_db;
    std::unique_ptr<QLockFile> m_lock;
};

DatabaseWorker::DatabaseWorker(QString path, QList<Migration> migrations, QObject* parent)
    : QObject(parent), m_path(std::move(path)),
      m_executor(new Executor(m_path, std::move(migrations)))
{
    m_thread.setObjectName(QStringLiteral("cirrus-db"));
    m_executor->moveToThread(&m_thread);
    m_thread.start();
}

DatabaseWorker::~DatabaseWorker()
{
    // FIFO: already queued jobs run first; their callbacks die with this object.
    QMetaObject::invokeMethod(
        m_executor,
        [executor = m_executor] {
            executor->close();
            QThread::currentThread()->quit();
        },
        Qt::QueuedConnection);
    m_thread.wait();
    delete m_executor;
}

void DatabaseWorker::open()
{
    Q_ASSERT(QThread::currentThread() == thread());
    QMetaObject::invokeMethod(
        m_executor,
        [this, executor = m_executor] {
            const auto error = executor->open();
            deliver([this, error] { emit opened(error); });
        },
        Qt::QueuedConnection);
}

void DatabaseWorker::post(Task task)
{
    QMetaObject::invokeMethod(
        m_executor, [executor = m_executor, task = std::move(task)] { executor->run(task); },
        Qt::QueuedConnection);
}

void DatabaseWorker::deliver(std::function<void()> callback)
{
    // Posted to this object: dropped automatically if it is destroyed first.
    QMetaObject::invokeMethod(this, std::move(callback), Qt::QueuedConnection);
}

} // namespace cirrus::storage
