#include "playlist_page_model.h"

#include "app.h"
#include "formatter.h"
#include "library.h"
#include "song_list_model.h"

namespace cirrus::ui
{

PlaylistPageModel::PlaylistPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    // Songs added to this playlist from elsewhere (a song's menu).
    if (const App* app = App::instance())
        connect(app->library(), &Library::playlistChanged, this, [this](const QString& key) {
            if (key == m_key)
                refresh();
        });
}

void PlaylistPageModel::setPlaylistKey(const QString& key)
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

Request PlaylistPageModel::load()
{
    return App::instance()->catalog()->playlist(m_key,
                                                handle<PlaylistPage>([this](PlaylistPage page) {
                                                    m_info = page.info;
                                                    emit headerChanged();
                                                    emit subscribedChanged();
                                                    m_detailRequests.clear();
                                                    m_pendingKeys.clear();
                                                    m_songs->reset(page.songKeys, page.songs);
                                                    m_savedOrder = page.songKeys;
                                                    updateMissingCount();
                                                    return page.songKeys.isEmpty();
                                                }));
}

QString PlaylistPageModel::updatedText() const
{
    const QString day = Formatter::date(m_info.updatedAtMs);
    return day.isEmpty() ? QString() : tr("更新于 %1").arg(day);
}

void PlaylistPageModel::setSubscribed(bool subscribed)
{
    if (subscribed == m_info.subscribed || m_info.own)
        return;
    m_info.subscribed = subscribed;
    emit subscribedChanged();
    m_subscribeRequest = App::instance()->librarySource()->setPlaylistSubscribed(
        m_key, subscribed, [this, subscribed](Outcome<bool> outcome) {
            m_subscribeRequest.release();
            if (outcome.value) {
                emit libraryChanged();
                return;
            }
            m_info.subscribed = !subscribed;
            emit subscribedChanged();
            const bool signedOut = outcome.failure && outcome.failure->kind == failure::SignedOut;
            emit notice(signedOut    ? tr("登录后才能收藏歌单")
                        : subscribed ? tr("没能收藏歌单，请稍后再试")
                                     : tr("没能取消收藏，请稍后再试"));
        });
}

void PlaylistPageModel::ensureLoaded(int first, int last)
{
    // Whole batches around the range, so scrolling on asks for few, full requests.
    first = first / kSongDetailsBatch * kSongDetailsBatch;
    last = (last / kSongDetailsBatch + 1) * kSongDetailsBatch - 1;
    requestDetails(m_songs->keysToLoad(first, last));
}

void PlaylistPageModel::loadAll()
{
    requestDetails(m_songs->keysToLoad(0, m_songs->rowCount() - 1));
}

void PlaylistPageModel::requestDetails(const QStringList& keys)
{
    QStringList batch;
    const auto send = [this, &batch]() {
        if (batch.isEmpty())
            return;
        const int id = m_nextDetailRequest++;
        for (const QString& key : std::as_const(batch))
            m_pendingKeys.insert(key);
        m_detailRequests[id] = App::instance()->catalog()->songDetails(
            batch, [this, id, keys = batch](Outcome<QList<SongInfo>> outcome) {
                m_detailRequests[id].release();
                m_detailRequests.erase(id);
                for (const QString& key : keys)
                    m_pendingKeys.remove(key);
                if (!outcome.value)
                    return;
                m_songs->fill(keys, *outcome.value);
                updateMissingCount();
            });
        batch.clear();
    };
    for (const QString& key : keys) {
        if (m_pendingKeys.contains(key))
            continue;
        batch << key;
        if (batch.size() == kSongDetailsBatch)
            send();
    }
    send();
}

void PlaylistPageModel::updateMissingCount()
{
    const int count = m_songs->missingCount();
    if (count == m_missingCount)
        return;
    m_missingCount = count;
    emit missingCountChanged();
}

QString PlaylistPageModel::failureMessage(const Outcome<bool>& outcome,
                                          const QString& otherwise) const
{
    if (outcome.failure && outcome.failure->kind == failure::SignedOut)
        return tr("登录已失效，请重新登录");
    if (outcome.failure && outcome.failure->kind == failure::Rejected)
        return tr("没能保存：只能修改自己创建的歌单");
    return otherwise;
}

void PlaylistPageModel::restoreSongs(const QStringList& keys, const QList<SongInfo>& songs)
{
    m_songs->reset(keys, songs);
}

void PlaylistPageModel::removeSongs(const QStringList& keys)
{
    if (!m_info.own || keys.isEmpty())
        return;
    const QStringList before = m_songs->keys();
    const QList<SongInfo> songs = m_songs->loadedSongs();
    m_songs->remove(keys);
    m_writeRequest = App::instance()->librarySource()->removeFromPlaylist(
        m_key, keys, [this, keys, before, songs](Outcome<bool> outcome) {
            m_writeRequest.release();
            if (outcome.value) {
                m_savedOrder = m_songs->keys();
                emit libraryChanged();
                emit notice(keys.size() == 1 ? tr("已从歌单移除")
                                             : tr("已从歌单移除 %1 首").arg(keys.size()));
                return;
            }
            restoreSongs(before, songs);
            emit notice(failureMessage(outcome, tr("没能从歌单移除，请稍后再试")));
        });
}

void PlaylistPageModel::moveSong(int from, int to)
{
    if (!m_info.own || from == to)
        return;
    m_songs->move(from, to);
    // Moves made while a save is on its way are saved after it.
    if (!m_orderRequest.pending())
        saveOrder();
}

void PlaylistPageModel::saveOrder()
{
    const QStringList order = m_songs->keys();
    if (order == m_savedOrder)
        return;
    const QStringList previous = m_savedOrder;
    m_savedOrder = order;
    m_orderRequest = App::instance()->librarySource()->reorderPlaylist(
        m_key, order, [this, previous](Outcome<bool> outcome) {
            m_orderRequest.release();
            if (outcome.value) {
                saveOrder();
                return;
            }
            m_savedOrder = previous;
            restoreSongs(previous, m_songs->loadedSongs());
            emit notice(failureMessage(outcome, tr("没能保存歌曲顺序，请稍后再试")));
        });
}

void PlaylistPageModel::updateInfo(const QString& title, const QString& description)
{
    const QString newTitle = title.trimmed();
    if (!m_info.own || newTitle.isEmpty())
        return;
    const PlaylistInfo before = m_info;
    m_info.title = newTitle;
    m_info.description = description.trimmed();
    emit headerChanged();
    m_writeRequest = App::instance()->librarySource()->updatePlaylist(
        m_key, m_info.title, m_info.description, [this, before](Outcome<bool> outcome) {
            m_writeRequest.release();
            if (outcome.value) {
                emit libraryChanged();
                emit notice(tr("已保存歌单信息"));
                return;
            }
            m_info.title = before.title;
            m_info.description = before.description;
            emit headerChanged();
            emit notice(failureMessage(outcome, tr("没能保存歌单信息，请稍后再试")));
        });
}

void PlaylistPageModel::deletePlaylist()
{
    if (!m_info.own)
        return;
    const QString title = m_info.title;
    m_writeRequest = App::instance()->librarySource()->deletePlaylist(
        m_key, [this, title](Outcome<bool> outcome) {
            m_writeRequest.release();
            if (outcome.value) {
                emit libraryChanged();
                emit notice(tr("已删除歌单「%1」").arg(title));
                emit deleted();
                return;
            }
            emit notice(failureMessage(outcome, tr("没能删除歌单，请稍后再试")));
        });
}

void PlaylistPageModel::clear()
{
    m_subscribeRequest.cancel();
    m_writeRequest.cancel();
    m_detailRequests.clear();
    m_pendingKeys.clear();
    m_orderRequest.cancel();
    m_savedOrder.clear();
    m_info = {};
    emit headerChanged();
    emit subscribedChanged();
    m_songs->clear();
    updateMissingCount();
}

} // namespace cirrus::ui
