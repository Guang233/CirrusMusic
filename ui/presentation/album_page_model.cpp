#include "album_page_model.h"

#include "app.h"
#include "formatter.h"
#include "song_list_model.h"

#include <QVariantMap>

namespace cirrus::ui
{

AlbumPageModel::AlbumPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
}

void AlbumPageModel::setAlbumKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit albumKeyChanged();
    if (key.isEmpty())
        reset();
    else
        reload();
}

QVariantList AlbumPageModel::artists() const
{
    QVariantList artists;
    for (const ArtistRef& artist : m_page.info.artists)
        artists << QVariantMap{{QStringLiteral("key"), artist.key},
                               {QStringLiteral("name"), artist.name}};
    return artists;
}

QString AlbumPageModel::artistText() const { return Formatter::artists(m_page.info.artists); }

QString AlbumPageModel::summary() const
{
    if (m_page.info.key.isEmpty())
        return {};
    QStringList parts;
    if (m_page.info.year > 0)
        parts << QString::number(m_page.info.year);
    parts << tr("%1 首").arg(Formatter::count(m_page.songKeys.size()));
    qint64 total = 0;
    bool known = !m_page.songs.isEmpty();
    for (const SongInfo& song : m_page.songs) {
        if (song.durationMs < 0)
            known = false;
        total += std::max<qint64>(song.durationMs, 0);
    }
    // Rounded up, so a few seconds don't read as "0 分钟".
    if (known)
        parts << tr("%1 分钟").arg((total + 59'999) / 60'000);
    return parts.join(tr(" · "));
}

Request AlbumPageModel::load()
{
    return App::instance()->catalog()->album(m_key, handle<AlbumPage>([this](AlbumPage page) {
                                                 m_page = std::move(page);
                                                 emit headerChanged();
                                                 m_songs->reset(m_page.songKeys, m_page.songs);
                                                 return m_page.songKeys.isEmpty();
                                             }));
}

void AlbumPageModel::clear()
{
    m_page = {};
    emit headerChanged();
    m_songs->clear();
}

} // namespace cirrus::ui
