#pragma once

#include "application/playback/listening_session.h"
#include "listening_state_store.h"

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
    // Called before the backend close is queued. Flush is bounded; never wait on the owner.
    void close();
    [[nodiscard]] bool writeRejected() const { return m_writeRejected; }
  signals:
    void requested(cirrus::storage::ListeningStateJob job);
    void storageFailed(cirrus::storage::StorageError error);
    void queueRestored(bool applied);

  private:
    void contextChanged();
    void queueChanged(const domain::QueueSnapshot& queue);
    void flush();
    bool live() const;
    application::ListeningSession& m_listening;
    application::BackendFacade& m_facade;
    domain::SessionSnapshot m_source;
    std::pair<quint64, quint64> m_restoreIntent;
    quint64 m_nextId = 0, m_loadId = 0;
    QSet<quint64> m_pending;
    std::optional<ListeningStateJob> m_save;
    QTimer m_timer;
    bool m_closed = false, m_restoring = false, m_writeRejected = false;
};
} // namespace cirrus::storage
