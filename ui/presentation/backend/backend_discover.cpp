// BackendCatalog, step 8 (docs/ui/integration.md §5): the home page's rows and charts,
// and the discover page's categories, playlists, artists and albums.

#include "backend_catalog.h"
#include "backend_mapping.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "domain/music/chart_types.h"
#include "domain/music/discovery_browse_types.h"
#include "domain/music/playlist_discovery_types.h"

#include <QDate>
#include <QSet>

namespace cirrus::ui
{
namespace
{

using namespace mapping;

// Cards a home row asks for; the row shows what fits (home.md §5).
constexpr int kShelf = 20;
// Preview lines of a chart card.
constexpr int kChartPreviews = 3;
// The service's most per page.
constexpr int kPage = 100;

// The service's name for "all categories".
const QString kAllCategories = QStringLiteral("全部");

QString categoryOf(const QString& category)
{
    return category.isEmpty() ? kAllCategories : category;
}

std::optional<domain::CatalogPage> catalogPage(PageRequest page)
{
    const domain::CatalogPage query{std::clamp(page.limit, 1, kPage), page.offset};
    if (!query.isValid())
        return std::nullopt;
    return query;
}

ResultPage<CollectionItem> noPage() { return ResultPage<CollectionItem>{{}, false, {}, {}}; }

// A row of cards: each once, in the service's order.
QList<CollectionItem> unique(QList<CollectionItem> items)
{
    QSet<QString> seen;
    QList<CollectionItem> result;
    for (CollectionItem& item : items)
        if (!seen.contains(item.key)) {
            seen.insert(item.key);
            result << std::move(item);
        }
    return result;
}

template <class Entry, class Map>
QList<CollectionItem> itemsOf(const QList<Entry>& entries, Map map)
{
    QList<CollectionItem> items;
    items.reserve(entries.size());
    for (const Entry& entry : entries)
        items << map(entry);
    return items;
}

// A discovery read's albums or artists (the facade matched the content to the query).
QList<CollectionItem> discoveredOf(const domain::DiscoveryBrowsePage& page)
{
    if (const auto* albums = std::get_if<domain::DiscoveryAlbums>(&page.content))
        return itemsOf(albums->albums, albumOf);
    if (const auto* artists = std::get_if<domain::DiscoveryArtists>(&page.content))
        return itemsOf(artists->artists, artistOf);
    return {};
}

ResultPage<CollectionItem> discoveredPageOf(const domain::DiscoveryBrowsePage& page)
{
    ResultPage<CollectionItem> result;
    result.items = discoveredOf(page);
    result.hasMore = page.hasMore;
    result.total = page.total;
    return result;
}

Request discover(MusicRequests* requests, domain::DiscoveryBrowseAction action,
                 Reply<ResultPage<CollectionItem>> reply)
{
    return read<domain::DiscoveryBrowsePage>(
        requests,
        [query = domain::DiscoveryBrowseQuery{std::move(action)}](BackendFacade& facade) {
            return facade.discover(query);
        },
        std::move(reply), discoveredPageOf);
}

ChartInfo chartOf(const domain::Chart& chart)
{
    ChartInfo info;
    info.key = chart.playlist.value;
    info.title = chart.title;
    info.updateLabel = chart.updateFrequency.value_or(QString());
    info.coverKey = coverKey(chart.cover);
    if (chart.previews)
        for (const auto& preview : chart.previews->mid(0, kChartPreviews))
            info.previews << ChartPreview{preview.primaryText,
                                          preview.secondaryText.value_or(QString())};
    return info;
}

// The names of a tag list, each once, without "all" (the page's own first chip).
QStringList namesOf(const QList<domain::PlaylistTag>& tags, const QString& all)
{
    QStringList names;
    for (const auto& tag : tags)
        if (tag.name != all && tag.name != kAllCategories && !names.contains(tag.name))
            names << tag.name;
    return names;
}

// The three category reads; settles once all three are back.
struct CategoriesRead {
    std::optional<domain::PlaylistTags> catalogue, hot, premium;
    std::optional<Failure> failure; // the catalogue's
    int waiting = 3;
    Request catalogueRead, hotRead, premiumRead;
};

PlaylistCategories categoriesOf(const CategoriesRead& read)
{
    PlaylistCategories result;
    const auto& catalogue = *read.catalogue;
    const QString all = catalogue.all ? catalogue.all->name : kAllCategories;
    if (catalogue.categories) {
        for (auto it = catalogue.categories->cbegin(); it != catalogue.categories->cend(); ++it) {
            QList<domain::PlaylistTag> tags;
            for (const auto& tag : catalogue.tags)
                if (tag.categoryCode == it.key())
                    tags << tag;
            const QStringList names = namesOf(tags, all);
            if (!names.isEmpty())
                result.groups << PlaylistCategories::Group{it.value(), names};
        }
    } else {
        // No group names: one group of them all.
        result.groups << PlaylistCategories::Group{QObject::tr("分类"),
                                                   namesOf(catalogue.tags, all)};
    }
    if (read.hot) {
        result.hot = namesOf(read.hot->tags, all);
    } else {
        // The hot read failed: those the catalogue marks hot.
        QList<domain::PlaylistTag> hot;
        for (const auto& tag : catalogue.tags)
            if (tag.hot == true)
                hot << tag;
        result.hot = namesOf(hot, all);
    }
    if (read.premium)
        result.premium = namesOf(read.premium->tags, all);
    return result;
}

} // namespace

Request BackendCatalog::feed(Feed, Reply<QList<FeedSection>> reply)
{
    return replyLater(this, std::move(reply), Outcome<QList<FeedSection>>{QList<FeedSection>{}});
}

Request BackendCatalog::shelf(feed::Shelf which, Reply<QList<CollectionItem>> reply)
{
    const auto playlists = [](const auto& found) {
        return unique(itemsOf(found.playlists, [](const domain::DiscoveryPlaylist& playlist) {
            return playlistOf(playlist);
        }));
    };
    switch (which) {
    case feed::DailyPlaylists:
        return read<domain::DailyRecommendedPlaylists>(
            m_requests,
            [](BackendFacade& facade) {
                return signedIn(facade) ? facade.dailyRecommendedPlaylists()
                                        : MusicRequests::NeedsSignIn;
            },
            std::move(reply), playlists);
    case feed::RecommendedPlaylists:
        return read<domain::RecommendedPlaylists>(
            m_requests, [](BackendFacade& facade) { return facade.recommendedPlaylists(kShelf); },
            std::move(reply), playlists);
    case feed::NewAlbums:
    case feed::PopularArtists: {
        domain::DiscoveryBrowseAction action = domain::LatestAlbumsQuery{};
        if (which == feed::PopularArtists)
            action = domain::PopularArtistsQuery{{kShelf, 0}};
        return read<domain::DiscoveryBrowsePage>(
            m_requests,
            [query = domain::DiscoveryBrowseQuery{action}](BackendFacade& facade) {
                return facade.discover(query);
            },
            std::move(reply),
            [](const domain::DiscoveryBrowsePage& page) {
                return unique(discoveredOf(page)).mid(0, kShelf);
            });
    }
    }
    return replyLater(this, std::move(reply),
                      Outcome<QList<CollectionItem>>{QList<CollectionItem>{}});
}

Request BackendCatalog::charts(Reply<QList<ChartInfo>> reply)
{
    return read<domain::Charts>(
        m_requests,
        [](BackendFacade& facade) { return facade.charts(domain::ChartListKind::WithPreviews); },
        std::move(reply),
        [](const domain::Charts& found) {
            QList<ChartInfo> charts;
            QSet<QString> seen;
            for (const auto& chart : found.charts)
                if (!seen.contains(chart.playlist.value)) {
                    seen.insert(chart.playlist.value);
                    charts << chartOf(chart);
                }
            return charts;
        });
}

Request BackendCatalog::playlistCategories(Reply<PlaylistCategories> reply)
{
    auto state = std::make_shared<CategoriesRead>();
    // Each read stores what it got; the last one back replies.
    const auto oneRead = [this, state,
                          reply](domain::PlaylistTagsKind kind,
                                 std::optional<domain::PlaylistTags> CategoriesRead::* slot) {
        const auto weak = std::weak_ptr<CategoriesRead>(state);
        return read<domain::PlaylistTags>(
            m_requests, [kind](BackendFacade& facade) { return facade.playlistTags(kind); },
            Reply<domain::PlaylistTags>(
                [weak, kind, slot, reply](Outcome<domain::PlaylistTags> outcome) {
                    const auto read = weak.lock();
                    if (!read)
                        return;
                    if (outcome.value)
                        (*read).*slot = std::move(*outcome.value);
                    else if (kind == domain::PlaylistTagsKind::Catalogue)
                        read->failure = outcome.failure;
                    if (--read->waiting > 0)
                        return;
                    if (!read->catalogue)
                        reply({std::nullopt, false,
                               read->failure.value_or(Failure{failure::Rejected, std::nullopt})});
                    else
                        reply({categoriesOf(*read), !read->hot || !read->premium, std::nullopt});
                }),
            [](const domain::PlaylistTags& tags) { return tags; });
    };
    state->catalogueRead = oneRead(domain::PlaylistTagsKind::Catalogue, &CategoriesRead::catalogue);
    state->hotRead = oneRead(domain::PlaylistTagsKind::Hot, &CategoriesRead::hot);
    state->premiumRead = oneRead(domain::PlaylistTagsKind::Premium, &CategoriesRead::premium);
    return Request([state] {
        state->catalogueRead.cancel();
        state->hotRead.cancel();
        state->premiumRead.cancel();
    });
}

Request BackendCatalog::categoryPlaylists(const QString& category, bool newest, PageRequest page,
                                          Reply<ResultPage<CollectionItem>> reply)
{
    const auto catalog = catalogPage(page);
    if (!catalog)
        return replyLater(this, std::move(reply), Outcome<ResultPage<CollectionItem>>{noPage()});
    const domain::PlaylistListQuery query{categoryOf(category), *catalog,
                                          newest ? domain::PlaylistOrder::New
                                                 : domain::PlaylistOrder::Hot};
    return read<domain::PlaylistListPage>(
        m_requests, [query](BackendFacade& facade) { return facade.playlistList(query); },
        std::move(reply),
        [](const domain::PlaylistListPage& found) {
            return pageOf<domain::PlaylistListPage, CollectionItem>(
                found, found.playlists,
                [](const domain::DiscoveryPlaylist& playlist) { return playlistOf(playlist); });
        });
}

Request BackendCatalog::premiumPlaylists(const QString& category, qint64 before, int limit,
                                         Reply<ResultPage<CollectionItem>> reply)
{
    const domain::PremiumPlaylistsQuery query{categoryOf(category), std::clamp(limit, 1, kPage),
                                              before};
    if (!query.isValid())
        return replyLater(this, std::move(reply), Outcome<ResultPage<CollectionItem>>{noPage()});
    return read<domain::PremiumPlaylistsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.premiumPlaylists(query); },
        std::move(reply),
        [](const domain::PremiumPlaylistsPage& found) {
            ResultPage<CollectionItem> result;
            result.items = itemsOf(found.playlists, [](const domain::DiscoveryPlaylist& playlist) {
                return playlistOf(playlist);
            });
            result.total = found.total;
            // The service's cursor: the last playlist's update time (playlist-discovery-
            // reads.md). Without it there is no next page to ask for.
            const bool more = found.hasMore.value_or(found.playlists.size() >= found.query.limit);
            if (more && !found.playlists.isEmpty() && found.playlists.last().providerUpdateTime &&
                *found.playlists.last().providerUpdateTime > 0 &&
                *found.playlists.last().providerUpdateTime != found.query.before)
                result.next = *found.playlists.last().providerUpdateTime;
            result.hasMore = result.next.has_value();
            return result;
        });
}

Request BackendCatalog::browseArtists(int kind, int area, const QString& initial, PageRequest page,
                                      Reply<ResultPage<CollectionItem>> reply)
{
    using domain::ArtistArea;
    using domain::ArtistCategory;
    static constexpr ArtistCategory kinds[] = {ArtistCategory::All, ArtistCategory::Male,
                                               ArtistCategory::Female, ArtistCategory::Band};
    static constexpr ArtistArea areas[] = {ArtistArea::All,     ArtistArea::Chinese,
                                           ArtistArea::Western, ArtistArea::Japanese,
                                           ArtistArea::Korean,  ArtistArea::Other};
    int letter = -1; // popular
    if (initial == QLatin1String("#"))
        letter = 0;
    else if (initial.size() == 1 && initial.at(0) >= QLatin1Char('A') &&
             initial.at(0) <= QLatin1Char('Z'))
        letter = initial.at(0).unicode();
    else if (!initial.isEmpty())
        letter = -2;
    const auto catalog = catalogPage(page);
    if (kind < 0 || kind > 3 || area < 0 || area > 5 || letter == -2 || !catalog)
        return replyLater(this, std::move(reply), Outcome<ResultPage<CollectionItem>>{noPage()});
    return discover(m_requests,
                    domain::ArtistBrowseQuery{kinds[kind], areas[area], letter, *catalog},
                    std::move(reply));
}

Request BackendCatalog::newAlbums(int area, PageRequest page,
                                  Reply<ResultPage<CollectionItem>> reply)
{
    // The page's areas are numbered as the service's (0 all, 1 Chinese, 2 Western,
    // 3 Korean, 4 Japanese).
    const auto catalog = catalogPage(page);
    if (area < 0 || area > 4 || !catalog)
        return replyLater(this, std::move(reply), Outcome<ResultPage<CollectionItem>>{noPage()});
    return discover(m_requests, domain::NewAlbumsQuery{domain::AlbumArea(area), *catalog},
                    std::move(reply));
}

Request BackendCatalog::albumChart(int area, bool monthly, Reply<QList<CollectionItem>> reply)
{
    if (area < 0 || area > 4)
        return replyLater(this, std::move(reply),
                          Outcome<QList<CollectionItem>>{QList<CollectionItem>{}});
    const QDate today = QDate::currentDate();
    domain::AlbumChartsQuery query;
    query.year = today.year();
    query.month = today.month();
    query.area = domain::AlbumArea(area);
    return m_requests->submit(
        [query = domain::DiscoveryBrowseQuery{query}](BackendFacade& facade) {
            return facade.discover(query);
        },
        [reply, monthly](const MusicResult& result) {
            const auto* page = std::get_if<domain::DiscoveryBrowsePage>(&result);
            const auto* charts =
                page ? std::get_if<domain::DiscoveryAlbumCharts>(&page->content) : nullptr;
            const auto& group = charts ? (monthly ? charts->month : charts->week)
                                       : std::optional<QList<domain::Album>>{};
            // A group the service left out is not known, not empty.
            if (!group) {
                reply({std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
                return;
            }
            reply({unique(itemsOf(*group, albumOf)), false, std::nullopt});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply({std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

} // namespace cirrus::ui
