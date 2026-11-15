#include "song_page_model.h"

#include "app.h"
#include "collection_list_model.h"
#include "formatter.h"
#include "song_list_model.h"

#include <QVariantMap>

namespace cirrus::ui
{

SongWikiModel::SongWikiModel(QObject* parent) : LoadableModel(parent) {}

QVariantList SongWikiModel::cards() const
{
    QVariantList cards;
    for (const SongWiki::Card& card : m_wiki.cards)
        cards << QVariantMap{{QStringLiteral("title"), card.title},
                             {QStringLiteral("values"), card.values}};
    return cards;
}

QVariantList SongWikiModel::sections() const
{
    QVariantList sections;
    for (const SongWiki::Section& section : m_wiki.sections) {
        QVariantList rows;
        for (const SongWiki::Row& row : section.rows)
            rows << QVariantMap{{QStringLiteral("label"), row.label},
                                {QStringLiteral("text"), row.text}};
        sections << QVariantMap{{QStringLiteral("title"), section.title},
                                {QStringLiteral("description"), section.description},
                                {QStringLiteral("rows"), rows}};
    }
    return sections;
}

void SongWikiModel::setSong(const SongInfo& song)
{
    m_song = song;
    if (song.key.isEmpty())
        reset();
    else
        reload();
}

Request SongWikiModel::load()
{
    return App::instance()->catalog()->songWiki(m_song, handle<SongWiki>([this](SongWiki wiki) {
                                                    m_wiki = std::move(wiki);
                                                    emit contentChanged();
                                                    return m_wiki.cards.isEmpty() &&
                                                           m_wiki.sections.isEmpty();
                                                }));
}

void SongWikiModel::clear()
{
    m_wiki = {};
    emit contentChanged();
}

SimilarSongsModel::SimilarSongsModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    followAccount();
}

void SimilarSongsModel::setSongKey(const QString& key)
{
    m_key = key;
    if (key.isEmpty())
        reset();
    else
        reload();
}

Request SimilarSongsModel::load()
{
    return App::instance()->catalog()->similarSongs(
        m_key, handle<QList<SongInfo>>([this](QList<SongInfo> songs) {
            QStringList keys;
            for (const SongInfo& song : std::as_const(songs))
                keys << song.key;
            m_songs->reset(keys, songs);
            return songs.isEmpty();
        }));
}

void SimilarSongsModel::clear() { m_songs->reset({}, {}); }

SimilarPlaylistsModel::SimilarPlaylistsModel(QObject* parent)
    : LoadableModel(parent), m_items(new CollectionListModel(this))
{
    followAccount();
}

void SimilarPlaylistsModel::setSongKey(const QString& key)
{
    m_key = key;
    if (key.isEmpty())
        reset();
    else
        reload();
}

Request SimilarPlaylistsModel::load()
{
    return App::instance()->catalog()->similarPlaylists(
        m_key, handle<QList<CollectionItem>>([this](QList<CollectionItem> items) {
            m_items->reset(items);
            return items.isEmpty();
        }));
}

void SimilarPlaylistsModel::clear() { m_items->reset({}); }

SongPageModel::SongPageModel(QObject* parent)
    : LoadableModel(parent), m_wiki(new SongWikiModel(this)),
      m_similarSongs(new SimilarSongsModel(this)),
      m_similarPlaylists(new SimilarPlaylistsModel(this))
{
    followAccount();
}

void SongPageModel::setSongKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit songKeyChanged();
    // What is like it doesn't need the song read first; the wiki does.
    m_similarSongs->setSongKey(key);
    m_similarPlaylists->setSongKey(key);
    m_wiki->setSong({});
    if (key.isEmpty())
        reset();
    else
        reload();
}

QString SongPageModel::dateText() const
{
    const QString date = Formatter::fullDate(m_song.publishedAtMs);
    return date.isEmpty() ? QString() : tr("%1发行").arg(date);
}

Request SongPageModel::load()
{
    return App::instance()->catalog()->songDetails(
        {m_key}, handle<QList<SongInfo>>([this](QList<SongInfo> songs) {
            m_song = songs.isEmpty() ? SongInfo{} : songs.constFirst();
            emit songChanged();
            m_wiki->setSong(m_song);
            return songs.isEmpty();
        }));
}

void SongPageModel::clear()
{
    m_song = {};
    emit songChanged();
}

} // namespace cirrus::ui
