#pragma once

#include "sources.h"

#include <QObject>

#include <memory>

namespace cirrus::application
{
class BackendFacade;
}
namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class MusicRequests;

// The read-only catalog from the backend (docs/ui/integration.md §4 CatalogSource),
// one group per step: search now (step 4); what is not connected yet goes to
// `fallback` (the fakes).
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

    // Not connected yet: the fallback's.
    Request playlist(const QString& key, Reply<PlaylistPage> reply) override
    {
        return m_fallback->playlist(key, std::move(reply));
    }
    Request songDetails(const QStringList& keys, Reply<QList<SongInfo>> reply) override
    {
        return m_fallback->songDetails(keys, std::move(reply));
    }
    Request album(const QString& key, Reply<AlbumPage> reply) override
    {
        return m_fallback->album(key, std::move(reply));
    }
    Request artist(const QString& key, Reply<ArtistPage> reply) override
    {
        return m_fallback->artist(key, std::move(reply));
    }
    Request artistSongs(const QString& key, bool byTime, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override
    {
        return m_fallback->artistSongs(key, byTime, page, std::move(reply));
    }
    Request artistAlbums(const QString& key, PageRequest page,
                         Reply<ResultPage<CollectionItem>> reply) override
    {
        return m_fallback->artistAlbums(key, page, std::move(reply));
    }
    Request artistIntroduction(const QString& key, Reply<ArtistIntroduction> reply) override
    {
        return m_fallback->artistIntroduction(key, std::move(reply));
    }
    Request similarArtists(const QString& key, Reply<QList<CollectionItem>> reply) override
    {
        return m_fallback->similarArtists(key, std::move(reply));
    }
    Request podcast(const QString& key, Reply<PodcastInfo> reply) override
    {
        return m_fallback->podcast(key, std::move(reply));
    }
    Request podcastVoices(const QString& key, bool oldestFirst, PageRequest page,
                          Reply<ResultPage<SongInfo>> reply) override
    {
        return m_fallback->podcastVoices(key, oldestFirst, page, std::move(reply));
    }
    Request feed(Feed which, Reply<QList<FeedSection>> reply) override
    {
        return m_fallback->feed(which, std::move(reply));
    }
    Request dailySongs(Reply<QList<SongInfo>> reply) override
    {
        return m_fallback->dailySongs(std::move(reply));
    }
    Request dailyHistoryDates(Reply<QStringList> reply) override
    {
        return m_fallback->dailyHistoryDates(std::move(reply));
    }
    Request dailyHistorySongs(const QString& date, Reply<QList<SongInfo>> reply) override
    {
        return m_fallback->dailyHistorySongs(date, std::move(reply));
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
};

} // namespace cirrus::ui
