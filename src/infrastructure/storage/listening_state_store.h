#pragma once

#include "database_worker.h"
#include "domain/playback/listening_state_types.h"

#include <optional>

namespace cirrus::storage
{
enum class ListeningStateOperation { LoadQueue, SaveQueue };
struct ListeningStateJob {
    quint64 id = 0;
    ListeningStateOperation operation{};
    QString accountId;
    domain::QueueSnapshot queue;
};
struct ListeningStateResult {
    quint64 id = 0;
    StorageError error = StorageError::None;
    std::optional<domain::QueueSnapshot> queue;
};

// Backend-thread adapter over the existing single DatabaseWorker/SQL executor.
class ListeningStateStore final : public QObject
{
    Q_OBJECT
  public:
    explicit ListeningStateStore(DatabaseWorker& database, QObject* parent = nullptr);
    static QList<Migration> migrations(); // released installation v1 unchanged, append only
    void submit(ListeningStateJob job);
    [[nodiscard]] qsizetype pending() const { return m_pending; }
    [[nodiscard]] bool writeFailed() const { return m_writeFailed; }
  signals:
    void completed(cirrus::storage::ListeningStateResult result);
    void idle();

  private:
    DatabaseWorker& m_database;
    qsizetype m_pending = 0;
    bool m_writeFailed = false;
};
} // namespace cirrus::storage
Q_DECLARE_METATYPE(cirrus::storage::ListeningStateJob)
Q_DECLARE_METATYPE(cirrus::storage::ListeningStateResult)
