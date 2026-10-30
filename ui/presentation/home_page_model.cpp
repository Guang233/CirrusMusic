#include "home_page_model.h"

#include "app.h"
#include "collection_list_model.h"
#include "song_list_model.h"

namespace cirrus::ui
{

namespace
{
QStringList keysOf(const QList<SongInfo>& songs)
{
    QStringList keys;
    keys.reserve(songs.size());
    for (const SongInfo& song : songs)
        keys << song.key;
    return keys;
}
} // namespace

void FeedSectionModel::reset(const QList<FeedSection>& sections)
{
    const int oldCount = rowCount();
    beginResetModel();
    for (const Row& row : std::as_const(m_rows)) {
        row.collections->deleteLater();
        row.songs->deleteLater();
    }
    m_rows.clear();
    for (const FeedSection& section : sections) {
        Row row{section.kind, section.title, new CollectionListModel(this),
                new SongListModel(this)};
        row.collections->reset(section.collections);
        row.songs->reset(keysOf(section.songs), section.songs);
        m_rows << row;
    }
    endResetModel();
    if (rowCount() != oldCount)
        emit countChanged();
}

int FeedSectionModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant FeedSectionModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row& row = m_rows.at(index.row());
    switch (role) {
    case KindRole:
        return int(row.kind);
    case TitleRole:
        return row.title;
    case CollectionsRole:
        return QVariant::fromValue(row.collections);
    case SongsRole:
        return QVariant::fromValue(row.songs);
    default:
        return {};
    }
}

QHash<int, QByteArray> FeedSectionModel::roleNames() const
{
    return {{KindRole, "kind"},
            {TitleRole, "title"},
            {CollectionsRole, "collections"},
            {SongsRole, "songs"}};
}

HomePageModel::HomePageModel(QObject* parent)
    : LoadableModel(parent), m_sections(new FeedSectionModel(this))
{
}

void HomePageModel::setFeed(int feed)
{
    if (feed != feed::Home && feed != feed::Discover)
        return;
    if (m_feed == feed)
        return;
    m_feed = feed;
    emit feedChanged();
    if (m_complete)
        reload();
}

void HomePageModel::componentComplete()
{
    m_complete = true;
    reload();
}

Request HomePageModel::load()
{
    return App::instance()->catalog()->feed(
        Feed(m_feed), handle<QList<FeedSection>>([this](QList<FeedSection> sections) {
            m_sections->reset(sections);
            return sections.isEmpty();
        }));
}

void HomePageModel::clear() { m_sections->reset({}); }

DailySongsPageModel::DailySongsPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    followAccount();
    reload();
}

Request DailySongsPageModel::load()
{
    return App::instance()->catalog()->dailySongs(
        handle<QList<SongInfo>>([this](QList<SongInfo> songs) {
            m_songs->reset(keysOf(songs), songs);
            return songs.isEmpty();
        }));
}

void DailySongsPageModel::clear() { m_songs->clear(); }

LibraryPageModel::LibraryPageModel(QObject* parent)
    : LoadableModel(parent), m_likedSongs(new SongListModel(this)),
      m_createdPlaylists(new CollectionListModel(this)),
      m_savedPlaylists(new CollectionListModel(this)), m_albums(new CollectionListModel(this)),
      m_artists(new CollectionListModel(this)), m_recentSongs(new SongListModel(this))
{
    followAccount();
    reload();
}

Request LibraryPageModel::load()
{
    return App::instance()->library()->library(handle<LibraryPage>([this](LibraryPage page) {
        m_likedSongs->reset(keysOf(page.likedSongs), page.likedSongs);
        m_createdPlaylists->reset(page.createdPlaylists);
        m_savedPlaylists->reset(page.savedPlaylists);
        m_albums->reset(page.albums);
        m_artists->reset(page.artists);
        m_recentSongs->reset(keysOf(page.recentSongs), page.recentSongs);
        return page.likedSongs.isEmpty() && page.createdPlaylists.isEmpty() &&
               page.savedPlaylists.isEmpty() && page.albums.isEmpty() && page.artists.isEmpty() &&
               page.recentSongs.isEmpty();
    }));
}

void LibraryPageModel::clear()
{
    m_likedSongs->clear();
    m_createdPlaylists->clear();
    m_savedPlaylists->clear();
    m_albums->clear();
    m_artists->clear();
    m_recentSongs->clear();
}

} // namespace cirrus::ui
