#include "api_operation.h"
#include "domain/music/catalog_types.h"
#include "domain/music/entity_search_types.h"
#include "domain/music/playlist_discovery_types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace cirrus::netease
{

std::optional<ApiOperationContract> operationContract(ApiOperation operation,
                                                      const QJsonObject& parameters)
{
    if (operation == ApiOperation::AlbumDetails || operation == ApiOperation::ArtistAlbums) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        if (!domain::validMusicId(id))
            return std::nullopt;
        const auto prefix = operation == ApiOperation::AlbumDetails
                                ? QStringLiteral("/api/v1/album/")
                                : QStringLiteral("/api/artist/albums/");
        return ApiOperationContract{prefix + id, Protocol::Weapi, ResponseEncoding::Json,
                                    8 * 1024 * 1024};
    }
    switch (operation) {
    case ApiOperation::AlbumDetails:
    case ApiOperation::ArtistAlbums:
        return std::nullopt; // resolved only from a validated canonical ID above
    case ApiOperation::ListeningStart:
    case ApiOperation::ListeningPlay:
        return ApiOperationContract{QStringLiteral("/api/feedback/weblog"), Protocol::Eapi,
                                    ResponseEncoding::Json, 64 * 1024,
                                    OfficialDestination::ListeningLog};
    case ApiOperation::RecommendedPlaylists:
        return ApiOperationContract{QStringLiteral("/api/personalized/playlist"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::PlaylistList:
        return ApiOperationContract{QStringLiteral("/api/playlist/list"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::PremiumPlaylists:
        return ApiOperationContract{QStringLiteral("/api/playlist/highquality/list"),
                                    Protocol::Weapi, ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::PlaylistCatalogue:
        return ApiOperationContract{QStringLiteral("/api/playlist/catalogue"), Protocol::Eapi,
                                    ResponseEncoding::Json, 1024 * 1024};
    case ApiOperation::HotPlaylistTags:
        return ApiOperationContract{QStringLiteral("/api/playlist/hottags"), Protocol::Weapi,
                                    ResponseEncoding::Json, 512 * 1024};
    case ApiOperation::PremiumPlaylistTags:
        return ApiOperationContract{QStringLiteral("/api/playlist/highquality/tags"),
                                    Protocol::Weapi, ResponseEncoding::Json, 1024 * 1024};
    case ApiOperation::Charts:
        return ApiOperationContract{QStringLiteral("/api/toplist"), Protocol::Eapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::ChartsWithPreviews:
        return ApiOperationContract{QStringLiteral("/api/toplist/detail"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::RecentSongs:
        return ApiOperationContract{QStringLiteral("/api/play-record/song/list"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::ListeningRanking:
        return ApiOperationContract{QStringLiteral("/api/v1/play/record"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::MyPlaylists:
        return ApiOperationContract{QStringLiteral("/api/user/playlist"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::LikedSongIds:
        return ApiOperationContract{QStringLiteral("/api/song/like/get"), Protocol::Eapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::SavedAlbums:
        return ApiOperationContract{QStringLiteral("/api/album/sublist"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::SavedArtists:
        return ApiOperationContract{QStringLiteral("/api/artist/sublist"), Protocol::Weapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::DailyRecommendations:
        return ApiOperationContract{QStringLiteral("/api/v3/discovery/recommend/songs"),
                                    Protocol::Weapi, ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::PersonalFm:
        return ApiOperationContract{QStringLiteral("/api/v1/radio/get"), Protocol::Weapi,
                                    ResponseEncoding::Json, 1024 * 1024};
    case ApiOperation::ArtistDetails:
        return ApiOperationContract{QStringLiteral("/api/artist/head/info/get"), Protocol::Eapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::ArtistSongs:
        return ApiOperationContract{QStringLiteral("/api/v1/artist/songs"), Protocol::Eapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::PlaylistDetails:
        return ApiOperationContract{QStringLiteral("/api/v6/playlist/detail"), Protocol::Eapi,
                                    ResponseEncoding::Json, 16 * 1024 * 1024};
    case ApiOperation::QrKey:
        return ApiOperationContract{QStringLiteral("/api/login/qrcode/unikey")};
    case ApiOperation::QrCheck:
        return ApiOperationContract{QStringLiteral("/api/login/qrcode/client/login")};
    case ApiOperation::AccountIdentity:
        return ApiOperationContract{QStringLiteral("/api/w/nuser/account/get"), Protocol::Weapi};
    case ApiOperation::SearchAlbums:
    case ApiOperation::SearchArtists:
    case ApiOperation::SearchPlaylists:
    case ApiOperation::SearchSongs:
        return ApiOperationContract{QStringLiteral("/api/search/get"), Protocol::Eapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::SearchVoices:
        return ApiOperationContract{QStringLiteral("/api/search/voice/get"), Protocol::Eapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::SearchPodcasts:
        return ApiOperationContract{QStringLiteral("/api/search/voicelist/get"), Protocol::Eapi,
                                    ResponseEncoding::EapiAutoJson, 2 * 1024 * 1024};
    case ApiOperation::SongDetails:
        return ApiOperationContract{QStringLiteral("/api/v3/song/detail"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::SongLyrics:
        return ApiOperationContract{QStringLiteral("/api/song/lyric/v1"), Protocol::Eapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::SongStream:
        return ApiOperationContract{QStringLiteral("/api/song/enhance/player/url/v1"),
                                    Protocol::Xeapi, ResponseEncoding::EapiAutoJson};
    case ApiOperation::AnonymousRegister:
        // xeapi responses are always AES-ECB encrypted, gzip detected by magic.
        return ApiOperationContract{QStringLiteral("/api/register/anonimous"), Protocol::Xeapi,
                                    ResponseEncoding::EapiAutoJson};
    }
    return std::nullopt;
}

bool requiresAccountCookie(ApiOperation operation)
{
    return operation == ApiOperation::AccountIdentity ||
           operation == ApiOperation::DailyRecommendations ||
           operation == ApiOperation::PersonalFm || operation == ApiOperation::MyPlaylists ||
           operation == ApiOperation::LikedSongIds || operation == ApiOperation::SavedAlbums ||
           operation == ApiOperation::SavedArtists || operation == ApiOperation::ListeningStart ||
           operation == ApiOperation::ListeningPlay || operation == ApiOperation::RecentSongs ||
           operation == ApiOperation::ListeningRanking;
}

bool validQrKey(QStringView key)
{
    if (key.isEmpty() || key.size() > 512) {
        return false;
    }
    for (const auto c : key) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e) {
            return false;
        }
    }
    return true;
}

std::optional<QJsonObject> operationPayload(ApiOperation operation, const QJsonObject& parameters)
{
    if (operation == ApiOperation::RecommendedPlaylists) {
        const auto limit = parameters.value(QStringLiteral("limit"));
        const domain::RecommendedPlaylistsQuery q{limit.toInt(-1)};
        if (parameters.size() != 1 || !q.isValid() || limit != QJsonValue(q.limit))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("limit"), q.limit},
                           {QStringLiteral("total"), true},
                           {QStringLiteral("n"), 1000}};
    }
    if (operation == ApiOperation::PlaylistList) {
        const auto cat = parameters.value(QStringLiteral("cat")).toString();
        const auto order = parameters.value(QStringLiteral("order")).toString();
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto offset = parameters.value(QStringLiteral("offset"));
        const domain::PlaylistListQuery q{cat,
                                          {limit.toInt(-1), offset.toInt(-1)},
                                          order == QStringLiteral("hot")
                                              ? domain::PlaylistOrder::Hot
                                              : domain::PlaylistOrder::New};
        if (parameters.size() != 4 || !q.isValid() ||
            (order != QStringLiteral("hot") && order != QStringLiteral("new")) ||
            limit != QJsonValue(q.page.limit) || offset != QJsonValue(q.page.offset))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("cat"), cat},
                           {QStringLiteral("order"), order},
                           {QStringLiteral("limit"), q.page.limit},
                           {QStringLiteral("offset"), q.page.offset},
                           {QStringLiteral("total"), true}};
    }
    if (operation == ApiOperation::PremiumPlaylists) {
        const auto cat = parameters.value(QStringLiteral("cat")).toString();
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto before = parameters.value(QStringLiteral("before"));
        const domain::PremiumPlaylistsQuery q{cat, limit.toInt(-1), before.toInteger(-1)};
        if (parameters.size() != 3 || !q.isValid() || limit != QJsonValue(q.limit) ||
            before != QJsonValue(q.before))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("cat"), cat},
                           {QStringLiteral("limit"), q.limit},
                           {QStringLiteral("lasttime"), q.before},
                           {QStringLiteral("total"), true}};
    }
    if (operation == ApiOperation::RecentSongs) {
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto n = limit.toInt(-1);
        if (parameters.size() != 1 || n < 1 || n > 100 || limit != QJsonValue(n))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("limit"), n}};
    }
    if (operation == ApiOperation::ListeningRanking) {
        const auto uid = parameters.value(QStringLiteral("uid")).toString();
        const auto type = parameters.value(QStringLiteral("type"));
        if (parameters.size() != 2 || !domain::validMusicId(uid) ||
            (type != QJsonValue(0) && type != QJsonValue(1)))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("uid"), uid}, {QStringLiteral("type"), type}};
    }
    if (operation == ApiOperation::ListeningStart || operation == ApiOperation::ListeningPlay) {
        const bool play = operation == ApiOperation::ListeningPlay;
        const auto id = parameters.value(QStringLiteral("id")).toString();
        const auto source = parameters.value(QStringLiteral("sourceId")).toString();
        const auto time = parameters.value(QStringLiteral("time"));
        const auto seconds = time.toInteger(-1);
        if (parameters.size() != (play ? 3 : 2) || !domain::validMusicId(id) ||
            !domain::validMusicId(source) ||
            (play && (seconds <= 0 || seconds > 604800 || time != QJsonValue(seconds))))
            return std::nullopt;
        QJsonObject json{{QStringLiteral("id"), id},
                         {QStringLiteral("type"), QStringLiteral("song")},
                         {QStringLiteral("mainsite"), QStringLiteral("1")},
                         {QStringLiteral("mainsiteWeb"), QStringLiteral("1")},
                         {QStringLiteral("content"), QStringLiteral("id=") + source}};
        if (play) {
            json.insert(QStringLiteral("download"), 0);
            json.insert(QStringLiteral("end"), QStringLiteral("playend"));
            json.insert(QStringLiteral("sourceId"), source);
            json.insert(QStringLiteral("time"), seconds);
            json.insert(QStringLiteral("wifi"), 0);
            json.insert(QStringLiteral("source"), QStringLiteral("list"));
        }
        const QJsonArray logs{QJsonObject{
            {QStringLiteral("action"), play ? QStringLiteral("play") : QStringLiteral("startplay")},
            {QStringLiteral("json"), json}}};
        return QJsonObject{{QStringLiteral("logs"),
                            QString::fromUtf8(QJsonDocument(logs).toJson(QJsonDocument::Compact))}};
    }
    if (operation == ApiOperation::AccountIdentity ||
        operation == ApiOperation::AnonymousRegister || operation == ApiOperation::PersonalFm) {
        return parameters.isEmpty() ? std::optional<QJsonObject>(QJsonObject{}) : std::nullopt;
    }
    if (operation == ApiOperation::DailyRecommendations) {
        if (!parameters.isEmpty())
            return std::nullopt;
        return QJsonObject{{QStringLiteral("afresh"), false}};
    }
    if (operation == ApiOperation::LikedSongIds) {
        const auto uid = parameters.value(QStringLiteral("uid")).toString();
        if (parameters.size() != 1 || !domain::validMusicId(uid))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("uid"), uid}};
    }
    if (operation == ApiOperation::MyPlaylists || operation == ApiOperation::SavedAlbums ||
        operation == ApiOperation::SavedArtists) {
        const bool playlists = operation == ApiOperation::MyPlaylists;
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto offset = parameters.value(QStringLiteral("offset"));
        const domain::CatalogPage page{limit.toInt(-1), offset.toInt(-1)};
        const auto uid = parameters.value(QStringLiteral("uid")).toString();
        if (parameters.size() != (playlists ? 3 : 2) || !page.isValid() ||
            limit != QJsonValue(page.limit) || offset != QJsonValue(page.offset) ||
            (playlists && !domain::validMusicId(uid)))
            return std::nullopt;
        QJsonObject payload{{QStringLiteral("limit"), page.limit},
                            {QStringLiteral("offset"), page.offset}};
        if (playlists) {
            payload.insert(QStringLiteral("uid"), uid);
            payload.insert(QStringLiteral("includeVideo"), false); // product excludes video
        } else
            payload.insert(QStringLiteral("total"), true);
        return payload;
    }
    if (operation == ApiOperation::AlbumDetails || operation == ApiOperation::ArtistDetails ||
        operation == ApiOperation::PlaylistDetails) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        if (parameters.size() != 1 || !domain::validMusicId(id))
            return std::nullopt;
        if (operation == ApiOperation::AlbumDetails)
            return QJsonObject{};
        if (operation == ApiOperation::ArtistDetails)
            return QJsonObject{{QStringLiteral("id"), id}};
        return QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("n"), 100000}, {QStringLiteral("s"), 8}};
    }
    if (operation == ApiOperation::ArtistSongs || operation == ApiOperation::ArtistAlbums) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto offset = parameters.value(QStringLiteral("offset"));
        const domain::CatalogPage page{limit.toInt(-1), offset.toInt(-1)};
        const bool songs = operation == ApiOperation::ArtistSongs;
        if (parameters.size() != (songs ? 4 : 3) || !domain::validMusicId(id) || !page.isValid() ||
            limit != QJsonValue(page.limit) || offset != QJsonValue(page.offset))
            return std::nullopt;
        QJsonObject payload{{QStringLiteral("limit"), page.limit},
                            {QStringLiteral("offset"), page.offset}};
        if (!songs) {
            payload.insert(QStringLiteral("total"), true);
            return payload;
        }
        const auto order = parameters.value(QStringLiteral("order")).toString();
        if (order != QStringLiteral("hot") && order != QStringLiteral("time"))
            return std::nullopt;
        payload.insert(QStringLiteral("id"), id);
        payload.insert(QStringLiteral("order"), order);
        payload.insert(QStringLiteral("private_cloud"), QStringLiteral("true"));
        payload.insert(QStringLiteral("work_type"), 1);
        return payload;
    }
    if (operation == ApiOperation::SearchAlbums || operation == ApiOperation::SearchArtists ||
        operation == ApiOperation::SearchPlaylists || operation == ApiOperation::SearchPodcasts ||
        operation == ApiOperation::SearchVoices) {
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto offset = parameters.value(QStringLiteral("offset"));
        const domain::SearchPage page{parameters.value(QStringLiteral("keywords")).toString(),
                                      limit.toInt(-1), offset.toInt(-1)};
        if (parameters.size() != 3 || !page.isValid() || limit != QJsonValue(page.limit) ||
            offset != QJsonValue(page.offset))
            return std::nullopt;
        QJsonObject p{{QStringLiteral("limit"), page.limit},
                      {QStringLiteral("offset"), page.offset}};
        if (operation == ApiOperation::SearchPodcasts || operation == ApiOperation::SearchVoices) {
            p.insert(QStringLiteral("keyword"), page.keywords.trimmed());
            p.insert(QStringLiteral("scene"), QStringLiteral("normal"));
            if (operation == ApiOperation::SearchPodcasts)
                p.insert(QStringLiteral("e_r"), true);
        } else {
            p.insert(QStringLiteral("s"), page.keywords.trimmed());
            p.insert(QStringLiteral("type"), operation == ApiOperation::SearchAlbums    ? 10
                                             : operation == ApiOperation::SearchArtists ? 100
                                                                                        : 1000);
        }
        return p;
    }
    if (operation == ApiOperation::SearchSongs) {
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto offset = parameters.value(QStringLiteral("offset"));
        const domain::SongSearch query{parameters.value(QStringLiteral("keywords")).toString(),
                                       limit.toInt(-1), offset.toInt(-1)};
        if (parameters.size() != 3 || !query.isValid() || limit != QJsonValue(query.limit) ||
            offset != QJsonValue(query.offset))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("s"), query.keywords},
                           {QStringLiteral("type"), 1},
                           {QStringLiteral("limit"), query.limit},
                           {QStringLiteral("offset"), query.offset}};
    }
    if (operation == ApiOperation::SongDetails) {
        const auto ids = parameters.value(QStringLiteral("ids")).toArray();
        if (parameters.size() != 1 || ids.isEmpty() || ids.size() > 100)
            return std::nullopt;
        QStringList objects;
        QSet<QString> seen;
        for (const auto value : ids) {
            const auto id = value.toString();
            if (!domain::validMusicId(id) || seen.contains(id))
                return std::nullopt;
            seen.insert(id);
            // c is a JSON string with integer literals; never round a wide ID through double.
            objects.append(QStringLiteral("{\"id\":") + id + u'}');
        }
        return QJsonObject{{QStringLiteral("c"), u'[' + objects.join(u',') + u']'}};
    }
    if (operation == ApiOperation::SongLyrics) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        if (parameters.size() != 1 || !domain::validMusicId(id))
            return std::nullopt;
        return QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("cp"), false}, {QStringLiteral("tv"), 0},
            {QStringLiteral("lv"), 0},  {QStringLiteral("rv"), 0},     {QStringLiteral("kv"), 0},
            {QStringLiteral("yv"), 0},  {QStringLiteral("ytv"), 0},    {QStringLiteral("yrv"), 0}};
    }
    if (operation == ApiOperation::SongStream) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        const auto level = parameters.value(QStringLiteral("level")).toString();
        if (parameters.size() != 2 || !domain::validMusicId(id) ||
            (level != QStringLiteral("standard") && level != QStringLiteral("exhigh") &&
             level != QStringLiteral("lossless") && level != QStringLiteral("hires")))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("ids"), u'[' + id + u']'},
                           {QStringLiteral("level"), level},
                           {QStringLiteral("encodeType"), QStringLiteral("flac")}};
    }
    QJsonObject payload{{QStringLiteral("type"), 3}};
    switch (operation) {
    case ApiOperation::Charts:
    case ApiOperation::ChartsWithPreviews:
    case ApiOperation::PlaylistCatalogue:
    case ApiOperation::HotPlaylistTags:
    case ApiOperation::PremiumPlaylistTags:
        if (parameters.isEmpty())
            return QJsonObject{};
        break;
    case ApiOperation::QrKey:
        if (parameters.isEmpty()) {
            return payload;
        }
        break;
    case ApiOperation::QrCheck: {
        const auto key = parameters.value(QStringLiteral("key"));
        if (parameters.size() == 1 && key.isString() && validQrKey(key.toString())) {
            payload.insert(QStringLiteral("key"), key);
            return payload;
        }
        break;
    }
    case ApiOperation::AccountIdentity:
    case ApiOperation::AnonymousRegister:
    case ApiOperation::SearchAlbums:
    case ApiOperation::SearchArtists:
    case ApiOperation::SearchPlaylists:
    case ApiOperation::SearchPodcasts:
    case ApiOperation::SearchVoices:
    case ApiOperation::SearchSongs:
    case ApiOperation::SongDetails:
    case ApiOperation::SongLyrics:
    case ApiOperation::SongStream:
    case ApiOperation::AlbumDetails:
    case ApiOperation::ArtistDetails:
    case ApiOperation::ArtistSongs:
    case ApiOperation::ArtistAlbums:
    case ApiOperation::PlaylistDetails:
    case ApiOperation::DailyRecommendations:
    case ApiOperation::PersonalFm:
    case ApiOperation::MyPlaylists:
    case ApiOperation::LikedSongIds:
    case ApiOperation::SavedAlbums:
    case ApiOperation::SavedArtists:
    case ApiOperation::ListeningStart:
    case ApiOperation::ListeningPlay:
    case ApiOperation::RecentSongs:
    case ApiOperation::ListeningRanking:
    case ApiOperation::RecommendedPlaylists:
    case ApiOperation::PlaylistList:
    case ApiOperation::PremiumPlaylists:
        break;
    }
    return std::nullopt;
}

bool acceptsResponseCookies(ApiOperation operation, const DecodedResponse& response)
{
    if (!response.ok() || !response.businessCode) {
        return false;
    }
    switch (operation) {
    case ApiOperation::QrKey:
        return *response.businessCode == 200 &&
               validQrKey(response.object.value(QStringLiteral("unikey")).toString());
    case ApiOperation::QrCheck:
        return *response.businessCode >= 801 && *response.businessCode <= 803;
    case ApiOperation::AccountIdentity:
        return *response.businessCode == 200; // staged only in its isolated request snapshot
    case ApiOperation::Charts:
    case ApiOperation::ChartsWithPreviews:
    case ApiOperation::RecommendedPlaylists:
    case ApiOperation::PlaylistList:
    case ApiOperation::PremiumPlaylists:
    case ApiOperation::PlaylistCatalogue:
    case ApiOperation::HotPlaylistTags:
    case ApiOperation::PremiumPlaylistTags:
    case ApiOperation::AnonymousRegister:
    case ApiOperation::SearchAlbums:
    case ApiOperation::SearchArtists:
    case ApiOperation::SearchPlaylists:
    case ApiOperation::SearchPodcasts:
    case ApiOperation::SearchVoices:
    case ApiOperation::SearchSongs:
    case ApiOperation::SongDetails:
    case ApiOperation::SongLyrics:
    case ApiOperation::SongStream:
    case ApiOperation::AlbumDetails:
    case ApiOperation::ArtistDetails:
    case ApiOperation::ArtistSongs:
    case ApiOperation::ArtistAlbums:
    case ApiOperation::PlaylistDetails:
    case ApiOperation::DailyRecommendations:
    case ApiOperation::PersonalFm:
    case ApiOperation::MyPlaylists:
    case ApiOperation::LikedSongIds:
    case ApiOperation::SavedAlbums:
    case ApiOperation::SavedArtists:
    case ApiOperation::ListeningStart:
    case ApiOperation::ListeningPlay:
    case ApiOperation::RecentSongs:
    case ApiOperation::ListeningRanking:
        return false; // business reads/writes must not mutate verified account credentials
    }
    return false;
}

} // namespace cirrus::netease
