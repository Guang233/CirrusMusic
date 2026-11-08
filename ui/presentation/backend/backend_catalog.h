#pragma once

#include "sources.h"

#include <QObject>

#include <memory>

namespace cirrus::application
{
class BackendFacade;
}
namespace cirrus::domain
{
struct DailyHistoryDates;
}
namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class MusicRequests;

// The read-only catalog from the backend (docs/ui/integration.md §4 CatalogSource),
// one group per step: search (step 4) and details with the daily songs (step 5); what
// is not connected yet goes to `fallback` (the fakes).
class BackendCatalog : public QObject, public CatalogSource
{
  public:
    BackendCatalog(runtime::BackendRuntime& runtime, std::unique_ptr<CatalogSource> fallback,
                   QObject* parent = nullptr);
    ~BackendCatalog() override;

    // Step 4: search.
    Request suggestions(const QString& text, Reply<QList<SearchSuggestion>> reply) override;
    Request hotKeywords(Reply<QList<HotKeyword>> reply) override;
    Request searchSongs(const QString& text, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override;
    Request searchCollections(collection::Kind kind, const QString& text, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) override;
    Request searchVoices(const QString& text, PageRequest page,
                         Reply<ResultPage<SongInfo>> reply) override;

    // Step 5: details.
    Request playlist(const QString& key, Reply<PlaylistPage> reply) override;
    Request songDetails(const QStringList& keys, Reply<QList<SongInfo>> reply) override;
    Request album(const QString& key, Reply<AlbumPage> reply) override;
    Request artist(const QString& key, Reply<ArtistPage> reply) override;
    Request artistSongs(const QString& key, bool byTime, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override;
    Request artistAlbums(const QString& key, PageRequest page,
                         Reply<ResultPage<CollectionItem>> reply) override;
    Request artistIntroduction(const QString& key, Reply<ArtistIntroduction> reply) override;
    Request similarArtists(const QString& key, Reply<QList<CollectionItem>> reply) override;
    Request podcast(const QString& key, Reply<PodcastInfo> reply) override;
    Request podcastVoices(const QString& key, bool oldestFirst, PageRequest page,
                          Reply<ResultPage<SongInfo>> reply) override;
    Request dailySongs(Reply<QList<SongInfo>> reply) override;
    Request dailyHistoryDates(Reply<QStringList> reply) override;
    Request dailyHistorySongs(const QString& date, Reply<QList<SongInfo>> reply) override;

    // Not connected yet: the fallback's.
    Request feed(Feed which, Reply<QList<FeedSection>> reply) override
    {
        return m_fallback->feed(which, std::move(reply));
    }
    Request shelf(feed::Shelf which, Reply<QList<CollectionItem>> reply) override
    {
        return m_fallback->shelf(which, std::move(reply));
    }
    Request charts(Reply<QList<ChartInfo>> reply) override
    {
        return m_fallback->charts(std::move(reply));
    }
    Request playlistCategories(Reply<PlaylistCategories> reply) override
    {
        return m_fallback->playlistCategories(std::move(reply));
    }
    Request categoryPlaylists(const QString& category, bool newest, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) override
    {
        return m_fallback->categoryPlaylists(category, newest, page, std::move(reply));
    }
    Request premiumPlaylists(const QString& category, qint64 before, int limit,
                             Reply<ResultPage<CollectionItem>> reply) override
    {
        return m_fallback->premiumPlaylists(category, before, limit, std::move(reply));
    }
    Request browseArtists(int kind, int area, const QString& initial, PageRequest page,
                          Reply<ResultPage<CollectionItem>> reply) override
    {
        return m_fallback->browseArtists(kind, area, initial, page, std::move(reply));
    }
    Request newAlbums(int area, PageRequest page, Reply<ResultPage<CollectionItem>> reply) override
    {
        return m_fallback->newAlbums(area, page, std::move(reply));
    }
    Request albumChart(int area, bool monthly, Reply<QList<CollectionItem>> reply) override
    {
        return m_fallback->albumChart(area, monthly, std::move(reply));
    }

  private:
    std::unique_ptr<CatalogSource> m_fallback;
    MusicRequests* m_requests;
    // The signed-in account's key (a playlist of its own), empty for guests.
    std::function<QString()> m_session;
    // The last list of past recommendation days: a day's songs are read only for a day
    // the service listed (domain::DailyHistorySongsQuery).
    std::shared_ptr<std::shared_ptr<const domain::DailyHistoryDates>> m_historyDates;
};

} // namespace cirrus::ui
