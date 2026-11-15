#pragma once

#include "database_worker.h"
#include "domain/music/music_types.h"
#include "domain/playback/listening_state_types.h"
#include "domain/playback/listening_stats_types.h"

#include <optional>

namespace cirrus::storage
{
enum class ListeningStateOperation {
    LoadQueue,
    SaveQueue,
    SaveHistory,
    ClearHistory,
    LoadHistory,
    LoadStats,    // stats: the query
    MissingSongs, // up to 100 heard songs without details, most recent first
    SaveSongs     // songs (details) and unavailable (the service has none) for heard songs
};
struct ListeningStateJob {
    quint64 id = 0;
    ListeningStateOperation operation{};
    QString accountId;
    domain::QueueSnapshot queue;
    QList<domain::StoredListeningRecord> records{}; // <=64 facts per transaction, no DB row cap
    std::optional<domain::ListeningHistoryCursor> cursor = std::nullopt;
    int pageSize = 100;
    std::optional<domain::ListeningStatsQuery> stats = std::nullopt;
    QList<domain::Song> songs{};         // SaveSongs, <=100
    QList<domain::SongId> unavailable{}; // SaveSongs, <=100
};
struct ListeningStateResult {
    quint64 id = 0;
    StorageError error = StorageError::None;
    std::optional<domain::QueueSnapshot> queue;
    std::optional<domain::ListeningHistoryPage> history = std::nullopt;
    std::optional<domain::ListeningStats> stats = std::nullopt;
    QList<domain::SongId> missingSongs{};
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
