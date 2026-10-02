#pragma once
#include "domain/playback/listening_types.h"
#include <functional>

namespace cirrus::application
{
// Copyable candidate state: caller commits only after a playback command is accepted.
class PlaybackQueue
{
  public:
    using RandomIndex = std::function<quint32(quint32)>;
    explicit PlaybackQueue(RandomIndex random = {});
    bool replace(const QList<domain::SongId>& songs);
    bool append(const QList<domain::SongId>& songs, bool playNext = false);
    bool select(QUuid id);
    bool next(bool naturalEnd = false, bool ignoreSingleRepeat = false);
    bool previous();
    bool remove(QUuid id);
    void clear();
    bool setModes(domain::QueueOrder order, domain::QueueRepeat repeat);
    [[nodiscard]] std::optional<domain::QueueEntry> current() const;
    [[nodiscard]] QUuid successor() const;
    [[nodiscard]] domain::QueueSnapshot snapshot() const { return m_state; }
    static constexpr qsizetype MaximumEntries = 10000;

  private:
    void reshuffle();
    bool choose(QUuid id, bool record);
    domain::QueueSnapshot m_state;
    QList<QUuid> m_order, m_history;
    qsizetype m_historyCursor = -1;
    QUuid m_insertTail;
    RandomIndex m_random;
};
} // namespace cirrus::application
