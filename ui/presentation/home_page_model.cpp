#include "home_page_model.h"

#include "app.h"
#include "collection_list_model.h"
#include "song_list_model.h"

#include <algorithm>

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
            summarize(songs);
            return songs.isEmpty();
        }));
}

void DailySongsPageModel::clear()
{
    m_songs->clear();
    summarize({});
}

void DailySongsPageModel::summarize(const QList<SongInfo>& songs)
{
    constexpr int kCovers = 4;
    SongItem lead = songs.isEmpty() ? SongItem() : SongItem(songs.first());
    QStringList covers;
    for (const SongInfo& song : songs) {
        if (covers.size() == kCovers)
            break;
        covers << song.coverKey;
    }
    if (lead == m_lead && covers == m_coverKeys)
        return;
    m_lead = std::move(lead);
    m_coverKeys = std::move(covers);
    emit summaryChanged();
}

RecentSongsModel::RecentSongsModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    followAccount();
}

void RecentSongsModel::setLimit(int limit)
{
    limit = std::clamp(limit, 1, 100);
    if (limit == m_limit)
        return;
    m_limit = limit;
    emit limitChanged();
    if (m_complete)
        reload();
}

void RecentSongsModel::componentComplete()
{
    m_complete = true;
    reload();
}

Request RecentSongsModel::load()
{
    return App::instance()->librarySource()->recentSongs(
        m_limit, handle<QList<SongInfo>>([this](QList<SongInfo> songs) {
            m_songs->reset(keysOf(songs), songs);
            return songs.isEmpty();
        }));
}

void RecentSongsModel::clear() { m_songs->clear(); }

ShelfModel::ShelfModel(QObject* parent)
    : LoadableModel(parent), m_items(new CollectionListModel(this))
{
    followAccount();
}

void ShelfModel::setShelf(int shelf)
{
    if (shelf < feed::DailyPlaylists || shelf > feed::PopularArtists || shelf == m_shelf)
        return;
    m_shelf = shelf;
    emit shelfChanged();
    if (m_complete)
        reload();
}

void ShelfModel::componentComplete()
{
    m_complete = true;
    reload();
}

Request ShelfModel::load()
{
    return App::instance()->catalog()->shelf(
        feed::Shelf(m_shelf), handle<QList<CollectionItem>>([this](QList<CollectionItem> items) {
            m_items->reset(items);
            return items.isEmpty();
        }));
}

void ShelfModel::clear() { m_items->clear(); }

void ChartListModel::reset(const QList<ChartInfo>& charts)
{
    const int oldCount = rowCount();
    beginResetModel();
    m_charts = charts;
    endResetModel();
    if (rowCount() != oldCount)
        emit countChanged();
}

int ChartListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_charts.size());
}

QVariant ChartListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const ChartInfo& chart = m_charts.at(index.row());
    switch (role) {
    case KeyRole:
        return chart.key;
    case TitleRole:
        return chart.title;
    case UpdateLabelRole:
        return chart.updateLabel;
    case CoverKeyRole:
        return chart.coverKey;
    case PreviewsRole: {
        QVariantList previews;
        for (const ChartPreview& preview : chart.previews)
            previews << QVariantMap{{QStringLiteral("title"), preview.title},
                                    {QStringLiteral("subtitle"), preview.subtitle}};
        return previews;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> ChartListModel::roleNames() const
{
    return {{KeyRole, "key"},
            {TitleRole, "title"},
            {UpdateLabelRole, "updateLabel"},
            {CoverKeyRole, "coverKey"},
            {PreviewsRole, "previews"}};
}

ChartsModel::ChartsModel(QObject* parent)
    : LoadableModel(parent), m_charts(new ChartListModel(this))
{
    reload();
}

Request ChartsModel::load()
{
    return App::instance()->catalog()->charts(
        handle<QList<ChartInfo>>([this](QList<ChartInfo> charts) {
            m_charts->reset(charts);
            return charts.isEmpty();
        }));
}

void ChartsModel::clear() { m_charts->reset({}); }

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
    return App::instance()->librarySource()->library(handle<LibraryPage>([this](LibraryPage page) {
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
