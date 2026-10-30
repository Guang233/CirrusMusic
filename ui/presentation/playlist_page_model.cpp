#include "playlist_page_model.h"

#include "app.h"
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
                                                    m_songs->reset(page.songKeys, page.songs);
                                                    return page.songKeys.isEmpty();
                                                }));
}

void PlaylistPageModel::clear()
{
    m_info = {};
    emit headerChanged();
    m_songs->clear();
}

} // namespace cirrus::ui
