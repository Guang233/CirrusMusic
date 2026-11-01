#include "playlist_page_model.h"

#include "app.h"
#include "formatter.h"
#include "song_list_model.h"

namespace cirrus::ui
{

PlaylistPageModel::PlaylistPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
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
    m_subscribeRequest = App::instance()->library()->setPlaylistSubscribed(
        m_key, subscribed, [this, subscribed](Outcome<bool> outcome) {
            m_subscribeRequest.release();
            if (outcome.value) {
                emit libraryChanged();
                return;
            }
            m_info.subscribed = !subscribed;
            emit subscribedChanged();
            const bool signedOut = outcome.failure && outcome.failure->kind == failure::SignedOut;
            emit writeFailed(signedOut    ? tr("登录后才能收藏歌单")
                             : subscribed ? tr("没能收藏歌单，请稍后再试")
                                          : tr("没能取消收藏，请稍后再试"));
        });
}

void PlaylistPageModel::clear()
{
    m_subscribeRequest.cancel();
    m_info = {};
    emit headerChanged();
    emit subscribedChanged();
    m_songs->clear();
}

} // namespace cirrus::ui
