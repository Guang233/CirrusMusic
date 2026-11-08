#include "player.h"

#include "song_detail_loader.h"
#include "song_list_model.h"
#include "sources.h"

#include <algorithm>

namespace cirrus::ui
{

Player::Player(PlaybackSource* source, QObject* parent)
    : QObject(parent), m_source(source), m_queue(new SongListModel(this)),
      m_queueDetails(new SongDetailLoader(m_queue, this))
{
    connect(m_queueDetails, &SongDetailLoader::filled, this, [this] {
        updateCurrent();
        updateUpcoming();
    });
    if (!m_source)
        return;
    m_source->setChangedHandler([this]() { update(); });
    update();
}

void Player::update()
{
    const PlaybackSnapshot& next = m_source->snapshot();
    const bool queueChanged = next.queue != m_snapshot.queue;
    const bool moved = queueChanged || next.queueIndex != m_snapshot.queueIndex;
    m_snapshot = next;
    const SongInfo& current = m_snapshot.current;
    const bool detailed = !current.key.isEmpty() && !current.title.isEmpty();
    if (queueChanged) {
        // Details come as views ask; those known already stay (a reorder keeps them).
        QList<SongInfo> known = m_queue->loadedSongs();
        if (detailed)
            known.append(current);
        m_queueDetails->clear();
        m_queue->reset(m_snapshot.queue, known);
    } else if (detailed && !m_queue->songAt(m_snapshot.queueIndex)) {
        m_queue->fill({current.key}, {current});
    }
    // The songs either side, ahead of a switch: their titles and covers show at once.
    if (moved && m_snapshot.queueIndex >= 0)
        ensureQueueLoaded(m_snapshot.queueIndex - 1, m_snapshot.queueIndex + 1);
    updateCurrent();
    emit changed();
    updateUpcoming();
    noticePreview();
}

void Player::updateCurrent()
{
    SongInfo info = m_snapshot.current;
    if (!info.key.isEmpty() && info.title.isEmpty()) {
        // The source is still getting its details; the queue may have them.
        if (const auto row = m_queue->songAt(m_snapshot.queueIndex); row && row->key == info.key)
            info = *row;
    }
    SongItem current(info);
    if (current != m_current) {
        m_current = std::move(current);
        emit currentChanged();
    }
}

void Player::updateUpcoming()
{
    const int count = int(m_snapshot.queue.size());
    int row = m_snapshot.queueIndex + 1;
    if (row >= count && m_snapshot.repeat == playback::RepeatAll)
        row = 0;
    QString key;
    if (m_snapshot.queueIndex >= 0 && row != m_snapshot.queueIndex)
        if (const auto song = m_queue->songAt(row))
            key = song->coverKey;
    if (key != m_upcomingCoverKey) {
        m_upcomingCoverKey = key;
        emit upcomingChanged();
    }
}

void Player::noticePreview()
{
    // Whether it is a preview is known once it plays. Said once for a run of previews,
    // not for every song of a queue a guest plays.
    const QString& key = m_snapshot.current.key;
    if (key.isEmpty() || key == m_previewKnownKey ||
        (m_snapshot.status != playback::Playing && m_snapshot.status != playback::Paused))
        return;
    const bool preview = m_snapshot.previewStartMs >= 0;
    const bool before = m_previewKnown;
    m_previewKnownKey = key;
    m_previewKnown = preview;
    if (preview && !before)
        emit notice(tr("这首歌只能试听片段"));
}

QString Player::failureMessage() const
{
    if (m_snapshot.status != playback::Failed)
        return {};
    if (m_snapshot.current.unavailable)
        return tr("这首歌暂时无法播放");
    switch (m_snapshot.failure ? m_snapshot.failure->kind : failure::Unknown) {
    case failure::Network:
    case failure::Timeout:
        return tr("网络连接失败，无法播放");
    case failure::SignedOut:
        return tr("登录后才能播放");
    case failure::Rejected:
        return tr("这首歌暂时无法播放");
    default:
        return tr("播放失败");
    }
}

void Player::playSongs(const QStringList& keys, int index)
{
    if (m_source && index >= 0 && index < keys.size())
        m_source->playSongs(keys, index);
}

void Player::playVoices(const QStringList& keys, int index)
{
    if (canPlayVoices() && index >= 0 && index < keys.size())
        m_source->playVoices(keys, index);
}

bool Player::canPlayVoices() const { return m_source && m_source->canPlayVoices(); }

void Player::playPause()
{
    if (m_source)
        m_source->playPause();
}

void Player::next()
{
    if (m_source)
        m_source->next();
}

void Player::previous()
{
    if (m_source)
        m_source->previous();
}

void Player::seek(qint64 positionMs)
{
    if (m_source && m_snapshot.seekable)
        m_source->seek(std::max<qint64>(positionMs, 0));
}

void Player::playAt(int index)
{
    if (m_source && index >= 0 && index < m_snapshot.queue.size())
        m_source->playAt(index);
}

void Player::removeAt(int index)
{
    if (!m_source || m_snapshot.mode != playback::Queue || index < 0 ||
        index >= m_snapshot.queue.size())
        return;
    // The rows change now; the snapshot that follows then matches them (no reset).
    m_queue->removeAt(index);
    m_snapshot.queue.removeAt(index);
    if (index < m_snapshot.queueIndex)
        --m_snapshot.queueIndex;
    m_source->removeAt(index);
    emit changed();
}

void Player::moveQueue(int from, int to)
{
    const int count = int(m_snapshot.queue.size());
    if (!m_source || m_snapshot.mode != playback::Queue || from < 0 || from >= count || to < 0 ||
        to >= count || from == to)
        return;
    m_queue->moveRow(from, to);
    m_snapshot.queue.move(from, to);
    // The current song moves with its row.
    int& current = m_snapshot.queueIndex;
    if (from == current)
        current = to;
    else if (from < current && to >= current)
        --current;
    else if (from > current && to <= current)
        ++current;
    m_source->move(from, to);
    emit changed();
}

void Player::ensureQueueLoaded(int first, int last)
{
    const int count = m_queue->rowCount();
    if (count == 0)
        return;
    m_queueDetails->ensureLoaded(std::clamp(first, 0, count - 1), std::clamp(last, 0, count - 1));
}

void Player::setVolume(double volume)
{
    if (m_source)
        m_source->setVolume(std::clamp(volume, 0.0, 1.0));
}

void Player::setOrder(int order)
{
    if (m_source && (order == playback::Sequential || order == playback::Shuffle))
        m_source->setOrder(playback::Order(order));
}

void Player::setRepeat(int repeat)
{
    if (m_source && repeat >= playback::RepeatOff && repeat <= playback::RepeatOne)
        m_source->setRepeat(playback::Repeat(repeat));
}

void Player::startFm()
{
    if (m_source)
        m_source->startFm();
}

} // namespace cirrus::ui
