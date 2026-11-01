#include "playlist_page_model.h"

#include "app.h"
#include "formatter.h"
#include "library.h"
#include "song_detail_loader.h"
#include "song_list_model.h"

namespace cirrus::ui
{

PlaylistPageModel::PlaylistPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this)),
      m_details(new SongDetailLoader(m_songs, this))
{
    connect(m_details, &SongDetailLoader::missingCountChanged, this,
            &PlaylistPageModel::missingCountChanged);
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
                                                    // Requests for the old rows end.
                                                    m_details->clear();
                                                    m_songs->reset(page.songKeys, page.songs);
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

void PlaylistPageModel::ensureLoaded(int first, int last) { m_details->ensureLoaded(first, last); }

void PlaylistPageModel::loadAll() { m_details->loadAll(); }

int PlaylistPageModel::missingCount() const { return m_details->missingCount(); }

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
                emit libraryChanged();
                emit notice(keys.size() == 1 ? tr("已从歌单移除")
                                             : tr("已从歌单移除 %1 首").arg(keys.size()));
                return;
            }
            restoreSongs(before, songs);
            emit notice(failureMessage(outcome, tr("没能从歌单移除，请稍后再试")));
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
    m_info = {};
    emit headerChanged();
    emit subscribedChanged();
    m_songs->clear();
    m_details->clear();
}

} // namespace cirrus::ui
