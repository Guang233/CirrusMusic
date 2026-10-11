#include "playback_queue.h"
#include <QRandomGenerator>
#include <algorithm>

namespace cirrus::application
{
using namespace domain;
PlaybackQueue::PlaybackQueue(RandomIndex random) : m_random(std::move(random))
{
    if (!m_random)
        m_random = [](quint32 bound) { return QRandomGenerator::global()->bounded(bound); };
}
void PlaybackQueue::clear()
{
    m_state.entries.clear();
    m_state.active = {};
    m_order.clear();
    m_history.clear();
    m_historyCursor = -1;
    m_insertTail = {};
}
bool PlaybackQueue::replace(const QList<SongId>& songs)
{
    if (songs.isEmpty() || songs.size() > MaximumEntries)
        return false;
    for (const auto& song : songs)
        if (!validMusicId(song.value))
            return false;
    clear();
    return append(songs);
}
bool PlaybackQueue::append(const QList<SongId>& songs, bool playNext)
{
    if (songs.isEmpty() || songs.size() > MaximumEntries - m_state.entries.size())
        return false;
    for (const auto& song : songs)
        if (!validMusicId(song.value))
            return false;
    QList<QueueEntry> entries;
    QList<QUuid> ids;
    for (const auto& song : songs) {
        const auto id = QUuid::createUuid();
        entries.append({id, song});
        ids.append(id);
    }
    if (playNext && !m_state.active.isNull()) {
        // An explicit next-play insertion supersedes the old forward navigation branch.
        while (m_history.size() > m_historyCursor + 1)
            m_history.removeLast();
        const auto tail = m_insertTail.isNull() ? m_state.active : m_insertTail;
        const auto i = m_order.indexOf(tail);
        const auto it = std::find_if(m_state.entries.begin(), m_state.entries.end(),
                                     [tail](const auto& e) { return e.id == tail; });
        const auto displayIndex = it - m_state.entries.begin();
        for (qsizetype n = 0; n < entries.size(); ++n) {
            m_order.insert(i + 1 + n, ids[n]);
            m_state.entries.insert(displayIndex + 1 + n, entries[n]);
        }
        m_insertTail = ids.last();
    } else {
        m_state.entries.append(entries);
        m_order.append(ids);
        if (m_state.order == QueueOrder::Shuffle && m_state.active.isNull())
            reshuffle();
    }
    return true;
}
void PlaybackQueue::reshuffle()
{
    for (qsizetype i = m_order.size() - 1; i > 0; --i) {
        const auto j = m_random(quint32(i + 1)) % quint32(i + 1);
        m_order.swapItemsAt(i, j);
    }
}
bool PlaybackQueue::setModes(QueueOrder order, QueueRepeat repeat)
{
    if ((order != QueueOrder::Sequential && order != QueueOrder::Shuffle) ||
        (repeat != QueueRepeat::Off && repeat != QueueRepeat::All && repeat != QueueRepeat::One))
        return false;
    if (order != m_state.order) {
        while (m_history.size() > m_historyCursor + 1)
            m_history.removeLast();
        m_order.clear();
        for (const auto& e : m_state.entries)
            m_order.append(e.id);
        if (order == QueueOrder::Shuffle) {
            m_order.removeAll(m_state.active);
            reshuffle();
            if (!m_state.active.isNull())
                m_order.prepend(m_state.active);
        }
        m_insertTail = {};
    }
    m_state.order = order;
    m_state.repeat = repeat;
    return true;
}
bool PlaybackQueue::choose(QUuid id, bool record)
{
    if (id.isNull() || !m_order.contains(id))
        return false;
    if (m_state.active.isNull() && m_state.order == QueueOrder::Shuffle) {
        m_order.removeAll(id);
        m_order.prepend(
            id); // first explicit selection must not discard the front of a shuffle round
    }
    if (record) {
        while (m_history.size() > m_historyCursor + 1)
            m_history.removeLast();
        if (m_history.isEmpty() || m_history.last() != id)
            m_history.append(id);
        if (m_history.size() > 1024)
            m_history.removeFirst();
        m_historyCursor = m_history.size() - 1;
    }
    if (m_state.active != id)
        m_insertTail = {};
    m_state.active = id;
    return true;
}
bool PlaybackQueue::select(QUuid id) { return choose(id, true); }
bool PlaybackQueue::next(bool naturalEnd, bool ignoreSingleRepeat)
{
    if (m_order.isEmpty())
        return false;
    if (naturalEnd && !ignoreSingleRepeat && m_state.repeat == QueueRepeat::One &&
        !m_state.active.isNull())
        return true;
    if (m_historyCursor + 1 < m_history.size())
        return choose(m_history[++m_historyCursor], false);
    const auto index = m_order.indexOf(m_state.active);
    if (index + 1 < m_order.size())
        return choose(m_order[index + 1], true);
    if (m_state.repeat == QueueRepeat::Off)
        return false;
    if (m_state.order == QueueOrder::Shuffle) {
        reshuffle();
        if (m_order.size() > 1 && m_order.first() == m_state.active)
            m_order.swapItemsAt(0, 1);
    }
    return choose(m_order.first(), true);
}
bool PlaybackQueue::canNext() const
{
    return !m_order.isEmpty() && (m_historyCursor + 1 < m_history.size() ||
                                  m_order.indexOf(m_state.active) + 1 < m_order.size() ||
                                  m_state.repeat != QueueRepeat::Off);
}
bool PlaybackQueue::previous()
{
    return m_historyCursor > 0 && choose(m_history[--m_historyCursor], false);
}
QUuid PlaybackQueue::successor() const
{
    const auto i = m_order.indexOf(m_state.active);
    return i >= 0 && i + 1 < m_order.size() ? m_order[i + 1] : QUuid{};
}
bool PlaybackQueue::remove(QUuid id)
{
    if (!m_order.contains(id))
        return false;
    m_order.removeAll(id);
    m_state.entries.removeIf([id](const auto& entry) { return entry.id == id; });
    for (qsizetype i = m_history.size(); i-- > 0;)
        if (m_history[i] == id) {
            m_history.removeAt(i);
            if (i <= m_historyCursor)
                --m_historyCursor;
        }
    if (m_state.active == id)
        m_state.active = {};
    if (m_insertTail == id)
        m_insertTail = {};
    return true;
}
std::optional<QueueEntry> PlaybackQueue::current() const
{
    for (const auto& entry : m_state.entries)
        if (entry.id == m_state.active)
            return entry;
    return std::nullopt;
}
} // namespace cirrus::application
