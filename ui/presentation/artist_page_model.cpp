#include "artist_page_model.h"

#include "app.h"
#include "collection_list_model.h"
#include "formatter.h"
#include "song_list_model.h"

namespace cirrus::ui
{

ArtistPageModel::ArtistPageModel(QObject* parent)
    : LoadableModel(parent), m_topSongs(new SongListModel(this)),
      m_albums(new CollectionListModel(this))
{
}

void ArtistPageModel::setArtistKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit artistKeyChanged();
    if (key.isEmpty())
        reset();
    else
        reload();
}

QString ArtistPageModel::summary() const
{
    QStringList parts;
    if (m_info.songCount >= 0)
        parts << tr("%1 首歌曲").arg(Formatter::count(m_info.songCount));
    if (m_info.albumCount >= 0)
        parts << tr("%1 张专辑").arg(Formatter::count(m_info.albumCount));
    return parts.join(tr(" · "));
}

Request ArtistPageModel::load()
{
    return App::instance()->catalog()->artist(
        m_key, handle<ArtistPage>([this](ArtistPage page) {
            m_info = page.info;
            emit headerChanged();
            QStringList keys;
            for (const SongInfo& song : std::as_const(page.topSongs))
                keys << song.key;
            m_topSongs->reset(keys, page.topSongs);
            m_albums->reset(page.albums);
            return page.topSongs.isEmpty() && page.albums.isEmpty();
        }));
}

void ArtistPageModel::clear()
{
    m_info = {};
    emit headerChanged();
    m_topSongs->clear();
    m_albums->clear();
}

} // namespace cirrus::ui
