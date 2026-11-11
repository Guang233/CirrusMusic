#include "playlist_order_model.h"
#include "notice.h"

#include "app.h"
#include "formatter.h"
#include "library.h"
#include "song_detail_loader.h"
#include "song_list_model.h"
#include "sources.h"

#include <QCollator>

namespace cirrus::ui
{

PlaylistOrderModel::PlaylistOrderModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this)),
      m_details(new SongDetailLoader(m_songs, this))
{
    connect(m_details, &SongDetailLoader::waitingChanged, this,
            &PlaylistOrderModel::waitingChanged);
    // A quick sort asked for before every song's details were there.
    connect(m_details, &SongDetailLoader::filled, this, [this]() {
        if (m_pendingSort >= 0 && m_songs->allLoaded())
            applySort(std::exchange(m_pendingSort, -1));
    });
    connect(m_details, &SongDetailLoader::waitingChanged, this, [this]() {
        // The fetch failed: the sort is dropped (asking again fetches again).
        if (m_pendingSort >= 0 && m_details->waiting() == 0 && !m_songs->allLoaded()) {
            m_pendingSort = -1;
            emit sortPendingChanged();
            emit notice(
                noticed("playlist_order.details_failed", tr("没能取回全部歌曲信息，请稍后再试")));
        }
    });
}

void PlaylistOrderModel::setPlaylistKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit playlistKeyChanged();
    if (key.isEmpty())
        reset();
    else
        reload();
}

int PlaylistOrderModel::waiting() const { return m_details->waiting(); }

Request PlaylistOrderModel::load()
{
    return App::instance()->catalog()->playlist(m_key,
                                                handle<PlaylistPage>([this](PlaylistPage page) {
                                                    m_title = page.info.title;
                                                    emit titleChanged();
                                                    m_details->clear();
                                                    m_songs->reset(page.songKeys, page.songs);
                                                    m_savedOrder = page.songKeys;
                                                    updateChanged();
                                                    return page.songKeys.isEmpty();
                                                }));
}

void PlaylistOrderModel::clear()
{
    m_saveRequest.cancel();
    m_details->clear();
    m_songs->clear();
    m_savedOrder.clear();
    m_title.clear();
    emit titleChanged();
    updateChanged();
}

void PlaylistOrderModel::updateChanged()
{
    const bool changed = m_songs->keys() != m_savedOrder;
    if (changed == m_changed)
        return;
    m_changed = changed;
    emit changedChanged();
}

void PlaylistOrderModel::moveRows(const QList<int>& rows, int gap)
{
    m_songs->moveRows(rows, gap);
    updateChanged();
}

void PlaylistOrderModel::reverse()
{
    m_songs->reverse();
    updateChanged();
}

void PlaylistOrderModel::ensureLoaded(int first, int last) { m_details->ensureLoaded(first, last); }

void PlaylistOrderModel::sortBy(int field)
{
    if (m_songs->allLoaded()) {
        applySort(field);
        return;
    }
    const bool wasPending = m_pendingSort >= 0;
    m_pendingSort = field;
    if (!wasPending)
        emit sortPendingChanged();
    m_details->loadAll();
}

void PlaylistOrderModel::applySort(int field)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const auto text = [field](const SongInfo& song) {
        switch (field) {
        case Artist:
            return Formatter::artists(song.artists);
        case Album:
            return song.albumTitle;
        default:
            return song.title;
        }
    };
    m_songs->sortRows([&](const SongInfo& a, const SongInfo& b) {
        if (field == Duration) {
            // Unknown durations (-1) last.
            if ((a.durationMs < 0) != (b.durationMs < 0))
                return a.durationMs >= 0;
            return a.durationMs < b.durationMs;
        }
        return collator.compare(text(a), text(b)) < 0;
    });
    emit sortPendingChanged();
    updateChanged();
}

void PlaylistOrderModel::save()
{
    if (!m_changed || saving())
        return;
    const QStringList order = m_songs->keys();
    m_saveRequest = App::instance()->librarySource()->reorderPlaylist(
        m_key, order, [this, order](Outcome<bool> outcome) {
            m_saveRequest.release();
            emit savingChanged();
            if (outcome.value) {
                m_savedOrder = order;
                updateChanged();
                App::instance()->library()->notifyPlaylistChanged(m_key);
                emit notice(noticed("playlist_order.saved", tr("已保存歌曲顺序")));
                emit saved();
                return;
            }
            const bool signedOut = outcome.failure && outcome.failure->kind == failure::SignedOut;
            emit notice(
                signedOut ? noticed("playlist_order.signed_out", tr("登录已失效，请重新登录"))
                : maybeSaved(outcome)
                    ? noticed("playlist_order.save_unknown", tr("可能没有保存成功，刷新看看"))
                    : noticed("playlist_order.save_failed", tr("没能保存歌曲顺序，请稍后再试")));
        });
    emit savingChanged();
}

} // namespace cirrus::ui
