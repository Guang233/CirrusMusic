#include "backend_catalog.h"

#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "domain/music/discovery_types.h"
#include "domain/music/recommendation_history_types.h"
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

// Fills in the covers `songs` lack from one song-details read, then replies; replies
// with `value` as it is when that read fails. Some reads (old search, artist songs)
// give no album covers.
template <class T>
void replyWithCovers(MusicRequests* requests, std::shared_ptr<Request> current, T value,
                     std::function<QList<SongInfo>*(T&)> songsOf, Reply<T> reply)
{
    QList<domain::SongId> ids;
    QStringList seen;
    for (const SongInfo& song : std::as_const(*songsOf(value))) {
        if (song.coverKey.isEmpty() && domain::validMusicId(song.key) && !seen.contains(song.key) &&
            ids.size() < kSongDetailsBatch) {
            seen.append(song.key);
            ids.append(domain::SongId{song.key});
        }
    }
    if (ids.isEmpty() || !requests) {
        reply(Outcome<T>{std::move(value), false, std::nullopt});
        return;
    }
    auto shared = std::make_shared<T>(std::move(value));
    *current =
        requests->submit([ids](BackendFacade& facade) { return facade.songDetails(ids); },
                         [shared, songsOf, reply](const MusicResult& result) {
                             if (const auto* details = std::get_if<domain::SongDetails>(&result)) {
                                 QHash<QString, QString> covers;
                                 for (const auto& song : details->songs)
                                     if (song.album && !song.album->cover.isEmpty())
                                         covers.insert(song.id.value, coverKey(song.album->cover));
                                 for (SongInfo& song : *songsOf(*shared))
                                     if (song.coverKey.isEmpty())
                                         song.coverKey = covers.value(song.key);
                             }
                             reply(Outcome<T>{*shared, false, std::nullopt});
                         },
                         [shared, reply](failure::Kind, std::optional<qint64>) {
                             reply(Outcome<T>{*shared, false, std::nullopt});
                         });
}

// A read of an R mapped to T whose songs then get their missing covers.
template <class R, class T, class Map>
Request readWithCovers(MusicRequests* requests, MusicRequests::Send send, Reply<T> reply, Map map,
                       std::type_identity_t<std::function<QList<SongInfo>*(T&)>> songsOf)
{
    auto current = std::make_shared<Request>();
    auto guard = QPointer<MusicRequests>(requests);
    *current = read<R>(
        requests, std::move(send),
        Reply<T>([weak = std::weak_ptr<Request>(current), guard, songsOf, reply](Outcome<T> found) {
            const auto current = weak.lock();
            if (!current || !found.value) {
                reply(std::move(found));
                return;
            }
            replyWithCovers<T>(guard.data(), current, std::move(*found.value), songsOf, reply);
        }),
        map);
    return Request([current] { *current = Request(); });
}

QList<SongInfo>* pageSongs(ResultPage<SongInfo>& page) { return &page.items; }
QList<SongInfo>* listSongs(QList<SongInfo>& songs) { return &songs; }

// The facade won't take a signed-in-only read from a guest; say so rather than wait.
bool signedIn(const BackendFacade& facade)
{
    return facade.snapshot().phase == domain::SessionPhase::Authenticated;
}

CollectionItem searchArtistOf(const domain::SearchArtist& artist) { return artistOf(artist); }

SongInfo programOf(const domain::Program& program)
{
    SongInfo info;
    info.key = program.id.value;
    info.title = program.title;
    info.voice = true;
    info.coverKey = coverKey(program.cover);
    if (program.podcast) {
        info.albumKey = program.podcast->id.value;
        info.albumTitle = program.podcast->title;
        if (info.coverKey.isEmpty())
            info.coverKey = coverKey(program.podcast->cover);
    }
    info.durationMs = program.providerDuration.value_or(-1);
    info.serial = program.serialNumber ? int(*program.serialNumber) : -1;
    return info;
}

ArtistInfo artistInfoOf(const domain::ArtistDetails& artist)
{
    ArtistInfo info;
    info.key = artist.id.value;
    info.name = artist.name;
    info.description = artist.description;
    info.avatarKey = coverKey(artist.portrait);
    info.songCount = artist.songCount ? int(*artist.songCount) : -1;
    info.albumCount = artist.albumCount ? int(*artist.albumCount) : -1;
    info.aliases =
        artist.translatedNames.value_or(QStringList()) + artist.aliases.value_or(QStringList());
    return info;
}

AlbumPage albumPageOf(const domain::AlbumDetails& details)
{
    AlbumPage page;
    const auto& album = details.album;
    page.info.key = album.id.value;
    page.info.title = album.title;
    page.info.artists = artistsOf(album.artists);
    page.info.year = yearOf(album.publishedAtMs);
    page.info.coverKey = coverKey(album.cover);
    page.info.description = album.description;
    page.info.type = album.subType.value_or(album.type.value_or(QString()));
    page.info.aliases = album.aliases.value_or(QStringList());
    page.info.publishedAtMs = album.publishedAtMs.value_or(0);
    page.info.company = album.company.value_or(QString());
    for (const auto& song : details.songs) {
        SongInfo info = songOf(song);
        if (info.coverKey.isEmpty())
            info.coverKey = page.info.coverKey; // an album's songs share its cover
        page.songKeys.append(info.key);
        page.songs.append(info);
    }
    return page;
}

} // namespace

BackendCatalog::BackendCatalog(runtime::BackendRuntime& runtime,
                               std::unique_ptr<CatalogSource> fallback, QObject* parent)
    : QObject(parent), m_fallback(std::move(fallback)),
      m_requests(new MusicRequests(runtime.facade(), this)),
      m_historyDates(std::make_shared<std::shared_ptr<const domain::DailyHistoryDates>>())
{
    m_session = [facade = QPointer<BackendFacade>(&runtime.facade())]() {
        if (!facade || facade->snapshot().phase != domain::SessionPhase::Authenticated)
            return QString();
        return facade->snapshot().accountId;
    };
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
    return readWithCovers<domain::SongSearchPage>(
        m_requests,
        [query = *query](BackendFacade& facade) {
            return facade.searchSongs({query.keywords, query.limit, query.offset});
        },
        std::move(reply),
        [](const domain::SongSearchPage& found) {
            return pageOf<domain::SongSearchPage, SongInfo>(found, found.songs, songOf);
        },
        pageSongs);
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

// Step 5: details.

Request BackendCatalog::playlist(const QString& key, Reply<PlaylistPage> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<PlaylistPage>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    return m_requests->submit(
        [key](BackendFacade& facade) { return facade.playlistDetails({key}); },
        [this, reply, guard = QPointer<BackendCatalog>(this)](const MusicResult& result) {
            const auto* details = std::get_if<domain::PlaylistDetails>(&result);
            if (!details || !guard) {
                reply(Outcome<PlaylistPage>{std::nullopt, false,
                                            Failure{failure::Rejected, std::nullopt}});
                return;
            }
            PlaylistPage page;
            auto& info = page.info;
            info.key = details->id.value;
            info.title = details->title;
            info.description = details->description;
            info.creator = details->creator ? details->creator->nickname : QString();
            info.coverKey = coverKey(details->cover);
            info.tags = details->tags.value_or(QStringList());
            info.updatedAtMs = details->updatedAtMs.value_or(0);
            info.playCount = details->playCount ? qint64(*details->playCount) : -1;
            info.subscriberCount = details->subscriberCount.value_or(-1);
            info.commentCount = details->commentCount.value_or(-1);
            const QString account = m_session ? m_session() : QString();
            info.own =
                !account.isEmpty() && details->creator && details->creator->publicUserId == account;
            for (const auto& song : details->songs)
                page.songs.append(songOf(song));
            // The whole index when the service proves it; else what came embedded.
            bool partial = false;
            if (details->trackIds && details->trackIdsComplete) {
                for (const auto& id : *details->trackIds)
                    page.songKeys.append(id.value);
            } else {
                for (const SongInfo& song : std::as_const(page.songs))
                    page.songKeys.append(song.key);
                partial = details->trackCount && *details->trackCount > page.songKeys.size();
            }
            reply(Outcome<PlaylistPage>{std::move(page), partial, std::nullopt});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply(Outcome<PlaylistPage>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

Request BackendCatalog::songDetails(const QStringList& keys, Reply<QList<SongInfo>> reply)
{
    QList<domain::SongId> ids;
    QStringList seen;
    for (const QString& key : keys.mid(0, kSongDetailsBatch)) {
        if (domain::validMusicId(key) && !seen.contains(key)) {
            seen.append(key);
            ids.append(domain::SongId{key});
        }
    }
    if (ids.isEmpty())
        return replyLater(this, std::move(reply), Outcome<QList<SongInfo>>{QList<SongInfo>{}});
    return read<domain::SongDetails>(
        m_requests, [ids](BackendFacade& facade) { return facade.songDetails(ids); },
        std::move(reply),
        [](const domain::SongDetails& details) {
            QList<SongInfo> songs;
            songs.reserve(details.songs.size());
            for (const auto& song : details.songs)
                songs.append(songOf(song)); // missing ones are left out (the interface)
            return songs;
        });
}

Request BackendCatalog::album(const QString& key, Reply<AlbumPage> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<AlbumPage>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    return m_requests->submit(
        [key](BackendFacade& facade) { return facade.albumDetails({key}); },
        [reply](const MusicResult& result) {
            if (const auto* details = std::get_if<domain::AlbumDetails>(&result))
                reply(Outcome<AlbumPage>{albumPageOf(*details), !details->complete, std::nullopt});
            else
                reply(Outcome<AlbumPage>{std::nullopt, false,
                                         Failure{failure::Rejected, std::nullopt}});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply(Outcome<AlbumPage>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

Request BackendCatalog::artist(const QString& key, Reply<ArtistPage> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<ArtistPage>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    // The header and the popular songs, at once; the page fails only with the header.
    struct Join {
        std::optional<Outcome<ArtistInfo>> info;
        std::optional<QList<SongInfo>> top;
        Request infoRead, topRead;
        Reply<ArtistPage> reply;
        void finish()
        {
            if (!info || !top || !reply)
                return;
            auto done = std::exchange(reply, {});
            if (!info->value) {
                done(Outcome<ArtistPage>{std::nullopt, false, info->failure});
                return;
            }
            done(Outcome<ArtistPage>{ArtistPage{*info->value, *top}, false, std::nullopt});
        }
    };
    auto join = std::make_shared<Join>();
    join->reply = std::move(reply);
    std::weak_ptr<Join> weak = join;
    join->infoRead = read<domain::ArtistDetails>(
        m_requests, [key](BackendFacade& facade) { return facade.artistDetails({key}); },
        Reply<ArtistInfo>([weak](Outcome<ArtistInfo> info) {
            if (const auto join = weak.lock()) {
                join->info = std::move(info);
                join->finish();
            }
        }),
        artistInfoOf);
    join->topRead = readWithCovers<domain::DiscoveryBrowsePage>(
        m_requests,
        [key](BackendFacade& facade) {
            return facade.discover({domain::ArtistTopSongsQuery{{key}}});
        },
        Reply<ResultPage<SongInfo>>([weak](Outcome<ResultPage<SongInfo>> top) {
            if (const auto join = weak.lock()) {
                join->top = top.value ? top.value->items : QList<SongInfo>();
                join->finish();
            }
        }),
        [](const domain::DiscoveryBrowsePage& page) {
            ResultPage<SongInfo> top;
            if (const auto* songs = std::get_if<domain::DiscoverySongs>(&page.content))
                for (const auto& song : songs->songs.mid(0, 50))
                    top.items.append(songOf(song));
            return top;
        },
        pageSongs);
    return Request([join] {
        join->reply = {};
        join->infoRead = Request();
        join->topRead = Request();
    });
}

Request BackendCatalog::artistSongs(const QString& key, bool byTime, PageRequest page,
                                    Reply<ResultPage<SongInfo>> reply)
{
    const domain::ArtistSongsQuery query{{key},
                                         {std::clamp(page.limit, 1, 100), page.offset},
                                         byTime ? domain::ArtistSongOrder::Time
                                                : domain::ArtistSongOrder::Hot};
    if (!query.isValid())
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    return readWithCovers<domain::ArtistSongsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.artistSongs(query); },
        std::move(reply),
        [](const domain::ArtistSongsPage& found) {
            return pageOf<domain::ArtistSongsPage, SongInfo>(found, found.songs, songOf);
        },
        pageSongs);
}

Request BackendCatalog::artistAlbums(const QString& key, PageRequest page,
                                     Reply<ResultPage<CollectionItem>> reply)
{
    const domain::ArtistAlbumsQuery query{{key}, {std::clamp(page.limit, 1, 100), page.offset}};
    if (!query.isValid())
        return replyLater(
            this, std::move(reply),
            Outcome<ResultPage<CollectionItem>>{ResultPage<CollectionItem>{{}, false, {}, {}}});
    return read<domain::ArtistAlbumsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.artistAlbums(query); },
        std::move(reply),
        [](const domain::ArtistAlbumsPage& found) {
            return pageOf<domain::ArtistAlbumsPage, CollectionItem>(found, found.albums, albumOf);
        });
}

Request BackendCatalog::artistIntroduction(const QString& key, Reply<ArtistIntroduction> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(this, std::move(reply),
                          Outcome<ArtistIntroduction>{std::nullopt, false,
                                                      Failure{failure::Rejected, std::nullopt}});
    return read<domain::ArtistIntroduction>(
        m_requests, [key](BackendFacade& facade) { return facade.artistIntroduction({key}); },
        std::move(reply),
        [](const domain::ArtistIntroduction& found) {
            ArtistIntroduction intro;
            intro.brief = found.briefDescription.value_or(QString());
            for (const auto& section : found.sections)
                intro.sections.append({section.title.value_or(QString()), section.text});
            return intro;
        });
}

Request BackendCatalog::similarArtists(const QString& key, Reply<QList<CollectionItem>> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(this, std::move(reply),
                          Outcome<QList<CollectionItem>>{QList<CollectionItem>{}});
    return read<domain::DiscoveryBrowsePage>(
        m_requests,
        [key](BackendFacade& facade) {
            return facade.discover({domain::SimilarArtistsQuery{{key}}});
        },
        std::move(reply),
        [](const domain::DiscoveryBrowsePage& page) {
            QList<CollectionItem> artists;
            if (const auto* found = std::get_if<domain::DiscoveryArtists>(&page.content))
                for (const auto& artist : found->artists)
                    artists.append(searchArtistOf(artist));
            return artists;
        });
}

Request BackendCatalog::podcast(const QString& key, Reply<PodcastInfo> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<PodcastInfo>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    return read<domain::PodcastDetails>(
        m_requests, [key](BackendFacade& facade) { return facade.podcastDetails({key}); },
        std::move(reply),
        [](const domain::PodcastDetails& details) {
            PodcastInfo info;
            info.key = details.podcast.id.value;
            info.title = details.podcast.title;
            info.description = details.podcast.description.value_or(QString());
            info.coverKey = coverKey(details.podcast.cover);
            info.voiceCount = details.podcast.voiceCount ? int(*details.podcast.voiceCount) : -1;
            return info;
        });
}

Request BackendCatalog::podcastVoices(const QString& key, bool oldestFirst, PageRequest page,
                                      Reply<ResultPage<SongInfo>> reply)
{
    const domain::PodcastProgramsQuery query{{key},
                                             {std::clamp(page.limit, 1, 100), page.offset},
                                             oldestFirst ? domain::ProgramOrder::OldestFirst
                                                         : domain::ProgramOrder::NewestFirst};
    if (!query.isValid())
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    return read<domain::PodcastProgramsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.podcastPrograms(query); },
        std::move(reply),
        [](const domain::PodcastProgramsPage& found) {
            return pageOf<domain::PodcastProgramsPage, SongInfo>(found, found.programs, programOf);
        });
}

Request BackendCatalog::dailySongs(Reply<QList<SongInfo>> reply)
{
    return readWithCovers<domain::DailyRecommendations>(
        m_requests,
        [](BackendFacade& facade) {
            return signedIn(facade) ? facade.dailyRecommendations() : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [](const domain::DailyRecommendations& found) {
            QHash<QString, QString> reasons;
            for (const auto& reason : found.reasons)
                reasons.insert(reason.song.value, reason.text);
            QList<SongInfo> songs;
            songs.reserve(found.songs.size());
            for (const auto& recommended : found.songs) {
                SongInfo info = songOf(recommended.song);
                info.reason = recommended.inlineReason.value_or(reasons.value(info.key));
                songs.append(info);
            }
            return songs;
        },
        listSongs);
}

Request BackendCatalog::dailyHistoryDates(Reply<QStringList> reply)
{
    return read<domain::DailyHistoryDates>(
        m_requests,
        [](BackendFacade& facade) {
            return signedIn(facade) ? facade.dailyRecommendationHistoryDates()
                                    : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [kept = m_historyDates](const domain::DailyHistoryDates& found) {
            *kept = std::make_shared<const domain::DailyHistoryDates>(found);
            QStringList dates;
            for (const QString& date : found.dates)
                if (!dates.contains(date)) // the service may repeat a day
                    dates.append(date);
            return dates;
        });
}

Request BackendCatalog::dailyHistorySongs(const QString& date, Reply<QList<SongInfo>> reply)
{
    return readWithCovers<domain::DailyHistorySongs>(
        m_requests,
        [kept = m_historyDates, date](BackendFacade& facade) -> quint64 {
            if (!signedIn(facade))
                return MusicRequests::NeedsSignIn;
            // Only a day from this account's last list (the page reads it first).
            const auto dates = *kept;
            if (!dates || dates->query.accountId != facade.snapshot().accountId)
                return MusicRequests::Refused;
            auto query =
                domain::DailyHistorySongsQuery::fromDates(facade.snapshot().token, *dates, date);
            if (!query)
                return MusicRequests::Refused;
            return facade.dailyRecommendationHistorySongs(std::move(*query));
        },
        std::move(reply),
        [](const domain::DailyHistorySongs& found) {
            QList<SongInfo> songs;
            songs.reserve(found.songs.size());
            for (const auto& song : found.songs)
                songs.append(songOf(song));
            return songs;
        },
        listSongs);
}

} // namespace cirrus::ui
