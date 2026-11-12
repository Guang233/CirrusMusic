#pragma once

#include "application/playback/listening_session.h"
#include "listening_state_store.h"

#include <QMap>
#include <QSet>
#include <QTimer>

namespace cirrus::storage
{
// Caller-thread composition; messages contain values only. No new playback authority.
class ListeningStateService final : public QObject
{
    Q_OBJECT
  public:
    ListeningStateService(application::ListeningSession& listening,
                          application::BackendFacade& facade, QObject* parent = nullptr);
    void deliver(ListeningStateResult result);
    // Bounded final handoff, drained by the existing worker without an owner-thread wait.
    QList<ListeningStateJob> close();
    quint64 historyPage(int pageSize = 100,
                        std::optional<domain::ListeningHistoryCursor> cursor = std::nullopt);
    // Local statistics of the live account (docs/api/local-listening-history.md §6): 0 when
    // refused (storageFailed), else the id statsReady answers. Records not yet written are
    // written first.
    quint64 stats(domain::ListeningStatsQuery query);
    // Answer to songsWanted: the details found and the songs the service has none for;
    // `ok` false when the lookup failed (tried again later).
    void resolveSongs(quint64 ticket, const QList<domain::Song>& songs,
                      const QList<domain::SongId>& unavailable, bool ok);
    [[nodiscard]] bool writeRejected() const { return m_writeRejected; }
  signals:
    void requested(cirrus::storage::ListeningStateJob job);
    void storageFailed(cirrus::storage::StorageError error);
    void queueRestored(bool applied);
    void historyPageReady(quint64 id, cirrus::storage::StorageError error,
                          cirrus::domain::ListeningHistoryPage page);
    void statsReady(quint64 id, cirrus::storage::StorageError error,
                    cirrus::domain::ListeningStats stats);
    // Heard songs without details (<=100): look them up (cache first) and answer with
    // resolveSongs(ticket, ...). The runtime connects this; without it details stay missing.
    void songsWanted(quint64 ticket, QList<cirrus::domain::SongId> songs);

  private:
    void contextChanged();
    void queueChanged(const domain::QueueSnapshot& queue);
    void flush();
    void recordChanged(domain::StoredListeningRecord record);
    void clearHistory(domain::SessionSnapshot source);
    void flushHistory();
    QList<ListeningStateJob> historyJobs(qsizetype maximumJobs);
    bool live() const;
    void scheduleSongs(int delayMs);
    void requestMissingSongs();
    void stopSongs();
    application::ListeningSession& m_listening;
    application::BackendFacade& m_facade;
    domain::SessionSnapshot m_source;
    std::pair<quint64, quint64> m_restoreIntent;
    quint64 m_nextId = 0, m_loadId = 0;
    QSet<quint64> m_pending;
    std::optional<ListeningStateJob> m_save;
    QTimer m_timer, m_historyTimer;
    QMap<QUuid, domain::StoredListeningRecord> m_historyDirty; // <=256 unsent facts, not retention
    QSet<QString> m_historyClears; // <=8 unsent scope erasures; always precede new facts
    struct HistoryRead {
        domain::SessionSnapshot source;
        quint64 epoch = 0;
        bool stats = false;
    };
    // Filling in song details: one step at a time (missing → wanted → saved → missing...).
    QTimer m_songTimer;
    quint64 m_songRead = 0, m_songTicket = 0, m_songSave = 0, m_nextTicket = 0;
    quint64 m_songEpoch = 0;
    domain::SessionSnapshot m_songSource;
    QList<domain::SongId> m_songsAsked; // the last ones asked for: the same again stops
    QHash<quint64, HistoryRead> m_historyReads;
    QHash<quint64, QString> m_clearRequests; // page admission waits for same-scope erase receipts
    quint64 m_historyEpoch = 0;
    bool m_closed = false, m_restoring = false, m_writeRejected = false;
};
} // namespace cirrus::storage
