#include "api_operation.h"
#include "domain/music/catalog_types.h"

#include <QJsonArray>
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
    case ApiOperation::SearchSongs:
        return ApiOperationContract{QStringLiteral("/api/search/get"), Protocol::Eapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
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
    if (operation == ApiOperation::AccountIdentity ||
        operation == ApiOperation::AnonymousRegister || operation == ApiOperation::PersonalFm) {
        return parameters.isEmpty() ? std::optional<QJsonObject>(QJsonObject{}) : std::nullopt;
    }
    if (operation == ApiOperation::DailyRecommendations) {
        if (!parameters.isEmpty())
            return std::nullopt;
        return QJsonObject{{QStringLiteral("afresh"), false}};
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
    case ApiOperation::AnonymousRegister:
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
        return false; // read-only metadata must not mutate verified account credentials
    }
    return false;
}

} // namespace cirrus::netease
