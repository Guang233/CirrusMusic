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
    [[nodiscard]] bool writeRejected() const { return m_writeRejected; }
  signals:
    void requested(cirrus::storage::ListeningStateJob job);
    void storageFailed(cirrus::storage::StorageError error);
    void queueRestored(bool applied);
    void historyPageReady(quint64 id, cirrus::storage::StorageError error,
                          cirrus::domain::ListeningHistoryPage page);

  private:
    void contextChanged();
    void queueChanged(const domain::QueueSnapshot& queue);
    void flush();
    void recordChanged(domain::StoredListeningRecord record);
    void clearHistory(domain::SessionSnapshot source);
    void flushHistory();
    QList<ListeningStateJob> historyJobs(qsizetype maximumJobs);
    bool live() const;
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
    };
    QHash<quint64, HistoryRead> m_historyReads;
    QHash<quint64, QString> m_clearRequests; // page admission waits for same-scope erase receipts
    quint64 m_historyEpoch = 0;
    bool m_closed = false, m_restoring = false, m_writeRejected = false;
};
} // namespace cirrus::storage
