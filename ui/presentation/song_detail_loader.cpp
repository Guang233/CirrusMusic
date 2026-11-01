#include "song_detail_loader.h"

#include "app.h"
#include "song_list_model.h"
#include "sources.h"

namespace cirrus::ui
{

SongDetailLoader::SongDetailLoader(SongListModel* songs, QObject* parent)
    : QObject(parent), m_songs(songs)
{
}

void SongDetailLoader::ensureLoaded(int first, int last)
{
    // Whole batches around the range, so scrolling on asks for few, full requests.
    first = first / kSongDetailsBatch * kSongDetailsBatch;
    last = (last / kSongDetailsBatch + 1) * kSongDetailsBatch - 1;
    queue(m_songs->keysToLoad(first, last), true);
}

void SongDetailLoader::loadAll() { queue(m_songs->keysToLoad(0, m_songs->rowCount() - 1), false); }

void SongDetailLoader::clear()
{
    m_request.cancel();
    m_queue.clear();
    const bool hadWaiting = !m_pendingKeys.isEmpty();
    m_pendingKeys.clear();
    if (hadWaiting)
        emit waitingChanged();
    updateMissingCount();
}

void SongDetailLoader::queue(const QStringList& keys, bool urgent)
{
    QList<QStringList> batches;
    for (const QString& key : keys) {
        if (m_pendingKeys.contains(key))
            continue;
        if (batches.isEmpty() || batches.last().size() == kSongDetailsBatch)
            batches.append(QStringList());
        batches.last() << key;
        m_pendingKeys.insert(key);
    }
    if (batches.isEmpty())
        return;
    if (urgent)
        m_queue = batches + m_queue;
    else
        m_queue += batches;
    emit waitingChanged();
    if (!m_request.pending())
        sendNext();
}

void SongDetailLoader::sendNext()
{
    if (m_queue.isEmpty())
        return;
    const QStringList keys = m_queue.takeFirst();
    m_request = App::instance()->catalog()->songDetails(
        keys, [this, keys](Outcome<QList<SongInfo>> outcome) {
            m_request.release();
            for (const QString& key : keys)
                m_pendingKeys.remove(key);
            if (outcome.value) {
                m_songs->fill(keys, *outcome.value);
                updateMissingCount();
                emit filled();
                emit waitingChanged();
                sendNext();
                return;
            }
            // Offline, rate limited…: asked for again next time.
            m_queue.clear();
            m_pendingKeys.clear();
            emit waitingChanged();
        });
}

void SongDetailLoader::updateMissingCount()
{
    const int count = m_songs->missingCount();
    if (count == m_missingCount)
        return;
    m_missingCount = count;
    emit missingCountChanged();
}

} // namespace cirrus::ui
