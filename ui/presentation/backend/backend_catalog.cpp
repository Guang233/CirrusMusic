#include "backend_catalog.h"

#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QTimeZone>
#include <QTimer>

namespace cirrus::ui
{
namespace
{

using application::BackendFacade;
using application::MusicResult;

// Replies after the call returned (never synchronously), unless canceled first.
template <class T> Request replyLater(QObject* context, Reply<T> reply, Outcome<T> outcome)
{
    auto alive = std::make_shared<bool>(true);
    QTimer::singleShot(0, context, [alive, reply = std::move(reply), outcome = std::move(outcome)] {
        if (*alive)
            reply(outcome);
    });
    return Request([alive] { *alive = false; });
}

// One read whose result is an R, mapped to the T the page wants.
template <class R, class T, class Map>
Request read(MusicRequests* requests, MusicRequests::Send send, Reply<T> reply, Map map)
{
    return requests->submit(
        std::move(send),
        [reply, map](const MusicResult& result) {
            if (const auto* value = std::get_if<R>(&result))
                reply(Outcome<T>{map(*value), false, std::nullopt});
            else
                reply(Outcome<T>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply(Outcome<T>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

// The backend's search page, or nullopt past what the service pages through.
std::optional<domain::SearchPage> searchPage(const QString& text, PageRequest page)
{
    domain::SearchPage query{text.trimmed(), std::clamp(page.limit, 1, 100), page.offset};
    if (!query.isValid())
        return std::nullopt;
    return query;
}

QString coverKey(const QUrl& url) { return url.isEmpty() ? QString() : url.toString(); }

int yearOf(const std::optional<qint64>& ms)
{
    return ms ? QDateTime::fromMSecsSinceEpoch(*ms, QTimeZone::UTC).date().year() : 0;
}

QList<ArtistRef> artistsOf(const QList<domain::ArtistCredit>& credits)
{
    QList<ArtistRef> artists;
    artists.reserve(credits.size());
    for (const auto& credit : credits)
        artists.append({credit.id ? credit.id->value : QString(), credit.name});
    return artists;
}

QString bylineOf(const QList<domain::ArtistCredit>& credits)
{
    QStringList names;
    for (const auto& credit : credits)
        names.append(credit.name);
    return names.join(QStringLiteral(" / "));
}

SongInfo songOf(const domain::Song& song)
{
    SongInfo info;
    info.key = song.id.value;
    info.title = song.title;
    if (song.translatedTitles && !song.translatedTitles->isEmpty())
        info.secondaryTitle = song.translatedTitles->first();
    else if (song.aliases && !song.aliases->isEmpty())
        info.secondaryTitle = song.aliases->first();
    info.artists = artistsOf(song.artists);
    if (song.album) {
        info.albumKey = song.album->id ? song.album->id->value : QString();
        info.albumTitle = song.album->title;
        info.coverKey = coverKey(song.album->cover);
    }
    info.durationMs = song.durationMs.value_or(-1);
    return info;
}

CollectionItem albumOf(const domain::Album& album)
{
    CollectionItem item;
    item.key = album.id.value;
    item.kind = collection::Album;
    item.title = album.title;
    item.byline = bylineOf(album.artists);
    item.year = yearOf(album.publishedAtMs);
    item.count = album.trackCount ? int(*album.trackCount) : -1;
    item.coverKey = coverKey(album.cover);
    return item;
}

CollectionItem artistOf(const domain::SearchArtist& artist)
{
    CollectionItem item;
    item.key = artist.id.value;
    item.kind = collection::Artist;
    item.title = artist.name;
    item.count = artist.albumCount ? int(*artist.albumCount) : -1;
    item.coverKey = coverKey(artist.portrait);
    return item;
}

CollectionItem playlistOf(const domain::DiscoveryPlaylist& playlist)
{
    CollectionItem item;
    item.key = playlist.id.value;
    item.kind = collection::Playlist;
    item.title = playlist.title;
    item.count = playlist.trackCount ? int(*playlist.trackCount) : -1;
    item.coverKey = coverKey(playlist.cover);
    item.playCount = playlist.playCount ? qint64(*playlist.playCount) : -1;
    return item;
}

CollectionItem podcastOf(const domain::Podcast& podcast)
{
    CollectionItem item;
    item.key = podcast.id.value;
    item.kind = collection::Podcast;
    item.title = podcast.title;
    item.count = podcast.voiceCount ? int(*podcast.voiceCount) : -1;
    item.coverKey = coverKey(podcast.cover);
    return item;
}

SongInfo voiceOf(const domain::Voice& voice)
{
    SongInfo info;
    info.key = voice.id.value;
    info.title = voice.title;
    info.voice = true;
    info.coverKey = coverKey(voice.cover);
    if (voice.podcast) {
        info.albumKey = voice.podcast->id.value;
        info.albumTitle = voice.podcast->title;
        if (info.coverKey.isEmpty())
            info.coverKey = coverKey(voice.podcast->cover);
    }
    // The provider's duration is raw metadata; milliseconds in what was seen so far.
    info.durationMs = voice.providerDuration.value_or(-1);
    return info;
}

template <class Page, class Item, class Map>
ResultPage<Item> pageOf(const Page& page, const auto& entries, Map map)
{
    ResultPage<Item> result;
    result.items.reserve(entries.size());
    for (const auto& entry : entries)
        result.items.append(map(entry));
    result.hasMore = page.hasMore;
    result.total = page.total;
    return result;
}

} // namespace

BackendCatalog::BackendCatalog(runtime::BackendRuntime& runtime,
                               std::unique_ptr<CatalogSource> fallback, QObject* parent)
    : QObject(parent), m_fallback(std::move(fallback)),
      m_requests(new MusicRequests(runtime.facade(), this))
{
}

BackendCatalog::~BackendCatalog() = default;

Request BackendCatalog::suggestions(const QString& text, Reply<QList<SearchSuggestion>> reply)
{
    const domain::SearchSuggestionsQuery query{text.trimmed()};
    if (!query.isValid())
        return replyLater(this, std::move(reply),
                          Outcome<QList<SearchSuggestion>>{QList<SearchSuggestion>{}});
    return read<domain::SearchSuggestions>(
        m_requests,
        [keywords = query.keywords](BackendFacade& facade) {
            return facade.searchSuggestions(keywords);
        },
        std::move(reply),
        [](const domain::SearchSuggestions& found) {
            QList<SearchSuggestion> list;
            QList<domain::SuggestionGroup> done;
            // In the service's group order, each group once.
            for (const auto group : found.order) {
                if (done.contains(group))
                    continue;
                done.append(group);
                switch (group) {
                case domain::SuggestionGroup::Song:
                    for (const auto& song :
                         found.songs.value_or(QList<domain::SearchSuggestion<domain::SongId>>{}))
                        list.append({true, collection::Playlist, song.id.value, song.title});
                    break;
                case domain::SuggestionGroup::Artist:
                    for (const auto& artist : found.artists.value_or(
                             QList<domain::SearchSuggestion<domain::ArtistId>>{}))
                        list.append({false, collection::Artist, artist.id.value, artist.title});
                    break;
                case domain::SuggestionGroup::Album:
                    for (const auto& album :
                         found.albums.value_or(QList<domain::SearchSuggestion<domain::AlbumId>>{}))
                        list.append({false, collection::Album, album.id.value, album.title});
                    break;
                case domain::SuggestionGroup::Playlist:
                    for (const auto& playlist : found.playlists.value_or(
                             QList<domain::SearchSuggestion<domain::PlaylistId>>{}))
                        list.append(
                            {false, collection::Playlist, playlist.id.value, playlist.title});
                    break;
                }
            }
            return list;
        });
}

Request BackendCatalog::hotKeywords(Reply<QList<HotKeyword>> reply)
{
    // The detailed list: it has the service's note for each keyword.
    return read<domain::HotKeywordDetails>(
        m_requests, [](BackendFacade& facade) { return facade.hotKeywordDetails(); },
        std::move(reply),
        [](const domain::HotKeywordDetails& details) {
            QList<HotKeyword> list;
            list.reserve(details.entries.size());
            for (const auto& entry : details.entries)
                list.append({entry.keyword, entry.description.value_or(QString())});
            return list;
        });
}

Request BackendCatalog::searchSongs(const QString& text, PageRequest page,
                                    Reply<ResultPage<SongInfo>> reply)
{
    const auto query = searchPage(text, page);
    if (!query)
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    // The search gives no album covers: the page's song details follow, and fill them in
    // (without covers when they fail). `current` is whichever read is running.
    struct Chain {
        Request current;
    };
    auto chain = std::make_shared<Chain>();
    auto requests = QPointer<MusicRequests>(m_requests);
    chain->current = read<domain::SongSearchPage>(
        m_requests,
        [query = *query](BackendFacade& facade) {
            return facade.searchSongs({query.keywords, query.limit, query.offset});
        },
        Reply<ResultPage<SongInfo>>([weak = std::weak_ptr<Chain>(chain), requests,
                                     reply](Outcome<ResultPage<SongInfo>> found) {
            const auto chain = weak.lock();
            if (!chain || !requests || !found.value || found.value->items.isEmpty()) {
                reply(std::move(found));
                return;
            }
            QList<domain::SongId> ids;
            QStringList seen;
            for (const SongInfo& song : std::as_const(found.value->items)) {
                if (song.coverKey.isEmpty() && domain::validMusicId(song.key) &&
                    !seen.contains(song.key)) {
                    seen.append(song.key);
                    ids.append(domain::SongId{song.key});
                }
            }
            if (ids.isEmpty()) {
                reply(std::move(found));
                return;
            }
            auto page = std::make_shared<ResultPage<SongInfo>>(std::move(*found.value));
            chain->current = requests->submit(
                [ids](BackendFacade& facade) { return facade.songDetails(ids); },
                [page, reply](const MusicResult& result) {
                    if (const auto* details = std::get_if<domain::SongDetails>(&result)) {
                        QHash<QString, QString> covers;
                        for (const auto& song : details->songs)
                            if (song.album && !song.album->cover.isEmpty())
                                covers.insert(song.id.value, coverKey(song.album->cover));
                        for (SongInfo& song : page->items)
                            if (song.coverKey.isEmpty())
                                song.coverKey = covers.value(song.key);
                    }
                    reply(Outcome<ResultPage<SongInfo>>{*page, false, std::nullopt});
                },
                [page, reply](failure::Kind, std::optional<qint64>) {
                    reply(Outcome<ResultPage<SongInfo>>{*page, false, std::nullopt});
                });
        }),
        [](const domain::SongSearchPage& found) {
            return pageOf<domain::SongSearchPage, SongInfo>(found, found.songs, songOf);
        });
    return Request([chain] { chain->current = Request(); });
}

Request BackendCatalog::searchCollections(collection::Kind kind, const QString& text,
                                          PageRequest page, Reply<ResultPage<CollectionItem>> reply)
{
    const auto query = searchPage(text, page);
    if (!query)
        return replyLater(
            this, std::move(reply),
            Outcome<ResultPage<CollectionItem>>{ResultPage<CollectionItem>{{}, false, {}, {}}});
    switch (kind) {
    case collection::Album:
        return read<domain::AlbumSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchAlbums(query); },
            std::move(reply),
            [](const domain::AlbumSearchPage& found) {
                return pageOf<domain::AlbumSearchPage, CollectionItem>(found, found.albums,
                                                                       albumOf);
            });
    case collection::Artist:
        return read<domain::ArtistSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchArtists(query); },
            std::move(reply),
            [](const domain::ArtistSearchPage& found) {
                return pageOf<domain::ArtistSearchPage, CollectionItem>(found, found.artists,
                                                                        artistOf);
            });
    case collection::Playlist:
        return read<domain::PlaylistSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchPlaylists(query); },
            std::move(reply),
            [](const domain::PlaylistSearchPage& found) {
                return pageOf<domain::PlaylistSearchPage, CollectionItem>(found, found.playlists,
                                                                          playlistOf);
            });
    case collection::Podcast:
        return read<domain::PodcastSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchPodcasts(query); },
            std::move(reply),
            [](const domain::PodcastSearchPage& found) {
                return pageOf<domain::PodcastSearchPage, CollectionItem>(found, found.podcasts,
                                                                         podcastOf);
            });
    }
    return replyLater(this, std::move(reply),
                      Outcome<ResultPage<CollectionItem>>{std::nullopt, false,
                                                          Failure{failure::Invalid, std::nullopt}});
}

Request BackendCatalog::searchVoices(const QString& text, PageRequest page,
                                     Reply<ResultPage<SongInfo>> reply)
{
    const auto query = searchPage(text, page);
    if (!query)
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    return read<domain::VoiceSearchPage>(
        m_requests, [query = *query](BackendFacade& f) { return f.searchVoices(query); },
        std::move(reply),
        [](const domain::VoiceSearchPage& found) {
            return pageOf<domain::VoiceSearchPage, SongInfo>(found, found.voices, voiceOf);
        });
}

} // namespace cirrus::ui
