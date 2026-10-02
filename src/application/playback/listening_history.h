#pragma once
#include "domain/playback/listening_history.h"

namespace cirrus::application
{
// Caller-thread value model. No disk/network, no accounting by observer arrival time.
class ListeningHistory
{
  public:
    static constexpr qsizetype MaximumRecords = 256;
    void begin(domain::LoadToken token, QString accountId, domain::SongId song,
               domain::ListeningMode source, QUuid queueEntry);
    bool observe(const domain::PlaybackSnapshot& snapshot);
    bool finish(domain::ListeningEnd end);
    void clear(); // forget rows and suppress this load; queued old totals cannot resurrect it
    void reset(); // account/lifetime change: forget everything
    [[nodiscard]] domain::SessionToken context() const { return m_context; }
    [[nodiscard]] QList<domain::ListeningRecord> records() const { return m_records; }

  private:
    domain::SessionToken m_context;
    std::optional<domain::ListeningRecord> m_active;
    QList<domain::ListeningRecord> m_records; // newest first, only positive observed loads
    quint64 m_generation = 0;
    qint64 m_lastTotal = 0;
};
} // namespace cirrus::application
