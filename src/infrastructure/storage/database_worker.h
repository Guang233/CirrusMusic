#pragma once

#include <QObject>
#include <QPointer>
#include <QSqlDatabase>
#include <QStringList>
#include <QThread>

#include <functional>
#include <memory>

namespace cirrus::storage
{

enum class StorageError {
    None,
    InvalidPath,
    Unavailable, // cannot create/open/configure the file
    Locked,      // another process holds the database lock
    Corrupt,     // integrity check failed or stored data violates invariants
    NewerSchema, // written by a newer program; never downgraded or written
    ChecksumMismatch,
    InsufficientSpace,
    BackupFailed,
    MigrationFailed,
    Query,
    NotOpen
};

template <typename T> struct StorageResult {
    StorageError error = StorageError::None;
    T value{};
    [[nodiscard]] bool ok() const { return error == StorageError::None; }
};

// Versioned schema step. Versions start at 1 and are contiguous. A released
// migration must never be edited: its SHA-256 is recorded and verified on open.
struct Migration {
    int version = 0;
    QStringList statements;
};

// One SQLite database owned by one dedicated thread (docs/09 §2). Callers submit
// jobs and receive results through queued callbacks in the worker's own thread;
// QSqlDatabase never crosses threads. Destruction drains already queued jobs,
// closes the connection and releases the process lock; their callbacks are dropped.
class DatabaseWorker final : public QObject
{
    Q_OBJECT
  public:
    DatabaseWorker(QString path, QList<Migration> migrations, QObject* parent = nullptr);
    ~DatabaseWorker() override;

    void open(); // once; result via opened()
    [[nodiscard]] const QString& path() const { return m_path; }

    // job runs on the worker thread; done runs in this object's thread, only while
    // context (same thread as this worker) is alive. Before a successful open, done
    // receives NotOpen without running job.
    template <typename T>
    void run(QObject* context, std::function<StorageResult<T>(QSqlDatabase&)> job,
             std::function<void(StorageResult<T>)> done);

  signals:
    void opened(cirrus::storage::StorageError error);

  private:
    class Executor;
    using Task = std::function<void(QSqlDatabase*)>;
    void post(Task task);
    void deliver(std::function<void()> callback);

    const QString m_path;
    QThread m_thread;
    Executor* m_executor = nullptr;
};

template <typename T>
void DatabaseWorker::run(QObject* context, std::function<StorageResult<T>(QSqlDatabase&)> job,
                         std::function<void(StorageResult<T>)> done)
{
    Q_ASSERT(QThread::currentThread() == thread() && context && context->thread() == thread());
    QPointer<QObject> target(context);
    post([this, target, job = std::move(job), done = std::move(done)](QSqlDatabase* db) mutable {
        auto result = db ? job(*db) : StorageResult<T>{StorageError::NotOpen, {}};
        deliver([target, done = std::move(done), result = std::move(result)]() mutable {
            if (target) {
                done(std::move(result));
            }
        });
    });
}

} // namespace cirrus::storage

Q_DECLARE_METATYPE(cirrus::storage::StorageError)
