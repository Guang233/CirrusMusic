#include "api_operation.h"
#include "collection_operation.h"
#include "comment_operation.h"
#include "comment_write_operation.h"
#include "discovery_browse_operation.h"
#include "domain/account/credential_login_types.h"
#include "domain/music/catalog_types.h"
#include "domain/music/entity_search_types.h"
#include "domain/music/playlist_discovery_types.h"
#include "domain/music/recommendation_history_types.h"
#include "domain/music/search_assist_types.h"
#include "together_operation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace cirrus::netease
{
namespace
{

QString stringParameter(const QJsonObject& parameters, QLatin1StringView name)
{
    const auto value = parameters.value(name);
    return value.isString() ? value.toString() : QString();
}

// Closed credential sign-in payloads, fixed fields as in the locked reference
// (captcha_sent.js, login_cellphone.js, login.js). nullopt for other operations too.
std::optional<QJsonObject> credentialPayload(ApiOperation operation, const QJsonObject& parameters)
{
    using namespace Qt::StringLiterals;
    if (operation == ApiOperation::SmsCodeSend) {
        const auto country = stringParameter(parameters, "ctcode"_L1);
        const auto phone = stringParameter(parameters, "cellphone"_L1);
        if (parameters.size() != 2 || !domain::validCountryCode(country) ||
            !domain::validPhoneNumber(phone))
            return std::nullopt;
        return QJsonObject{{u"ctcode"_s, country},
                           {u"secrete"_s, u"music_middleuser_pclogin"_s},
                           {u"cellphone"_s, phone}};
    }
    if (operation == ApiOperation::CellphoneLogin) {
        const auto country = stringParameter(parameters, "countrycode"_L1);
        const auto phone = stringParameter(parameters, "phone"_L1);
        const auto captcha = stringParameter(parameters, "captcha"_L1);
        const auto password = stringParameter(parameters, "password"_L1);
        const bool byCode = parameters.contains("captcha"_L1);
        if (parameters.size() != 3 || !domain::validCountryCode(country) ||
            !domain::validPhoneNumber(phone) ||
            (byCode ? !domain::validSmsCode(captcha) : !domain::validPasswordDigest(password)))
            return std::nullopt;
        QJsonObject payload{{u"type"_s, u"1"_s},        {u"https"_s, u"true"_s},
                            {u"phone"_s, phone},        {u"countrycode"_s, country},
                            {u"remember"_s, u"true"_s}, {u"secureCaptcha"_s, QString()}};
        if (byCode)
            payload.insert(u"captcha"_s, captcha);
        else
            payload.insert(u"password"_s, password);
        return payload;
    }
    if (operation == ApiOperation::EmailLogin) {
        const auto email = stringParameter(parameters, "username"_L1);
        const auto password = stringParameter(parameters, "password"_L1);
        if (parameters.size() != 2 || !domain::validEmail(email) ||
            !domain::validPasswordDigest(password))
            return std::nullopt;
        return QJsonObject{{u"type"_s, u"0"_s},
                           {u"https"_s, u"true"_s},
                           {u"username"_s, email},
                           {u"password"_s, password},
                           {u"rememberLogin"_s, u"true"_s}};
    }
    return std::nullopt;
}

} // namespace

std::optional<ApiOperationContract> operationContract(ApiOperation operation,
                                                      const QJsonObject& parameters)
{
    if (isDiscoveryBrowseOperation(operation))
        return discoveryBrowseContract(operation);
    if (isTogetherOperation(operation))
        return togetherContract(operation);
    if (isCommentWriteOperation(operation))
        return commentWriteContract(operation);
    if (isCollectionOperation(operation))
        return collectionOperationContract(operation);
    if (isCommentOperation(operation))
        return commentOperationContract(operation, parameters);
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
    case ApiOperation::LatestAlbums:
    case ApiOperation::NewAlbums:
    case ApiOperation::AlbumCharts:
    case ApiOperation::ArtistBrowse:
    case ApiOperation::PopularArtists:
    case ApiOperation::ArtistTopSongs:
    case ApiOperation::SimilarSongs:
    case ApiOperation::SimilarArtists:
    case ApiOperation::SimilarPlaylists:
        return std::nullopt; // closed discovery-browse adapter above
    case ApiOperation::TogetherCreate:
    case ApiOperation::TogetherCheck:
    case ApiOperation::TogetherJoin:
    case ApiOperation::TogetherStatus:
    case ApiOperation::TogetherEndCheck:
    case ApiOperation::TogetherEnd:
    case ApiOperation::TogetherHeartbeat:
    case ApiOperation::TogetherPlay:
    case ApiOperation::TogetherQueue:
    case ApiOperation::TogetherReplaceQueue:
    case ApiOperation::TogetherReconnectInfo:
    case ApiOperation::TogetherDeviceNotice:
        return std::nullopt; // closed HTTP-only adapter above
    case ApiOperation::SearchSuggestions:
        return ApiOperationContract{QStringLiteral("/api/search/suggest/web"), Protocol::Weapi,
                                    ResponseEncoding::Json, 512 * 1024};
    case ApiOperation::HotKeywords:
        return ApiOperationContract{QStringLiteral("/api/search/hot"), Protocol::Eapi,
                                    ResponseEncoding::Json, 512 * 1024};
    case ApiOperation::HotKeywordDetails:
        return ApiOperationContract{QStringLiteral("/api/hotsearchlist/get"), Protocol::Weapi,
                                    ResponseEncoding::Json, 1024 * 1024};
    case ApiOperation::CommentLike:
    case ApiOperation::CommentUnlike:
    case ApiOperation::CommentPublish:
    case ApiOperation::CommentReply:
    case ApiOperation::CommentDelete:
    case ApiOperation::SongHeart:
    case ApiOperation::PlaylistCollectSongs:
    case ApiOperation::AlbumSubscribe:
    case ApiOperation::AlbumUnsubscribe:
    case ApiOperation::ArtistSubscribe:
    case ApiOperation::ArtistUnsubscribe:
    case ApiOperation::PodcastSubscribe:
    case ApiOperation::PodcastUnsubscribe:
    case ApiOperation::PlaylistCreate:
    case ApiOperation::PlaylistRename:
    case ApiOperation::PlaylistDescription:
    case ApiOperation::PlaylistDelete:
    case ApiOperation::PlaylistSongOrder:
    case ApiOperation::AccountPlaylistOrder:
    case ApiOperation::PlaylistTagsUpdate:
    case ApiOperation::PlaylistMakePublic:
    case ApiOperation::CoverAllocate:
    case ApiOperation::PlaylistCoverCommit:
        return std::nullopt; // handled by the closed collection adapter above
    case ApiOperation::SongComments:
    case ApiOperation::AlbumComments:
    case ApiOperation::PlaylistComments:
    case ApiOperation::ProgramComments:
    case ApiOperation::HotComments:
    case ApiOperation::SortedComments:
    case ApiOperation::CommentFloor:
    case ApiOperation::CommentInfo:
        return std::nullopt; // handled by the closed comment adapter above
    case ApiOperation::AlbumDetails:
    case ApiOperation::ArtistAlbums:
        return std::nullopt; // resolved only from a validated canonical ID above
    case ApiOperation::ListeningStart:
    case ApiOperation::ListeningPlay:
        return ApiOperationContract{QStringLiteral("/api/feedback/weblog"), Protocol::Eapi,
                                    ResponseEncoding::Json, 64 * 1024,
                                    OfficialDestination::ListeningLog};
    case ApiOperation::DailyHistoryDates:
        return ApiOperationContract{QStringLiteral("/api/discovery/recommend/songs/history/recent"),
                                    Protocol::Weapi, ResponseEncoding::Json, 1024 * 1024};
    case ApiOperation::DailyHistorySongs:
        return ApiOperationContract{QStringLiteral("/api/discovery/recommend/songs/history/detail"),
                                    Protocol::Weapi, ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::DailyRecommendedPlaylists:
        return ApiOperationContract{QStringLiteral("/api/v1/discovery/recommend/resource"),
                                    Protocol::Weapi, ResponseEncoding::Json, 1024 * 1024};
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
    case ApiOperation::PodcastDetails:
        return ApiOperationContract{QStringLiteral("/api/djradio/v2/get"), Protocol::Weapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::PodcastPrograms:
        return ApiOperationContract{QStringLiteral("/api/dj/program/byradio"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::ProgramDetails:
        return ApiOperationContract{QStringLiteral("/api/dj/program/detail"), Protocol::Weapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::RecentPlaylists:
        return ApiOperationContract{QStringLiteral("/api/play-record/playlist/list"),
                                    Protocol::Weapi, ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::RecentAlbums:
        return ApiOperationContract{QStringLiteral("/api/play-record/album/list"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::RecentPodcasts:
        return ApiOperationContract{QStringLiteral("/api/play-record/djradio/list"),
                                    Protocol::Weapi, ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::RecentVoices:
        return ApiOperationContract{QStringLiteral("/api/play-record/voice/list"), Protocol::Weapi,
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
    case ApiOperation::SubscribedPodcasts:
        return ApiOperationContract{QStringLiteral("/api/djradio/get/subed"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::CloudSongs:
        return ApiOperationContract{QStringLiteral("/api/v1/cloud/get"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::CloudSongDetails:
        return ApiOperationContract{QStringLiteral("/api/v1/cloud/get/byids"), Protocol::Weapi,
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
    case ApiOperation::ArtistIntroduction:
        return ApiOperationContract{QStringLiteral("/api/artist/introduction"), Protocol::Weapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::SongWikiText:
        return ApiOperationContract{QStringLiteral("/api/link/page/parent/relation/construct/info"),
                                    Protocol::Eapi, ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::SongWikiSummary:
        return ApiOperationContract{QStringLiteral("/api/song/play/about/block/page"),
                                    Protocol::Eapi, ResponseEncoding::Json, 2 * 1024 * 1024};
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
    // Credential sign-in (docs/api/credential-login.md): protocols follow the locked
    // reference (captcha_sent/login_cellphone weapi; login eapi by default).
    case ApiOperation::SmsCodeSend:
        return ApiOperationContract{QStringLiteral("/api/sms/captcha/sent"), Protocol::Weapi};
    case ApiOperation::CellphoneLogin:
        return ApiOperationContract{QStringLiteral("/api/w/login/cellphone"), Protocol::Weapi,
                                    ResponseEncoding::Json, 256 * 1024};
    case ApiOperation::EmailLogin:
        return ApiOperationContract{QStringLiteral("/api/w/login"), Protocol::Eapi,
                                    ResponseEncoding::Json, 256 * 1024};
    // The PC client's search (cloudsearch.js): better ranking than /api/search/get and
    // songs with album covers; same result keys (docs/api/entity-search-reads.md).
    case ApiOperation::SearchAlbums:
    case ApiOperation::SearchArtists:
    case ApiOperation::SearchPlaylists:
    case ApiOperation::SearchSongs:
        return ApiOperationContract{QStringLiteral("/api/cloudsearch/pc"), Protocol::Eapi,
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
    return isTogetherOperation(operation) || isCommentWriteOperation(operation) ||
           isCollectionOperation(operation) || operation == ApiOperation::AccountIdentity ||
           operation == ApiOperation::DailyRecommendations ||
           operation == ApiOperation::DailyRecommendedPlaylists ||
           operation == ApiOperation::DailyHistoryDates ||
           operation == ApiOperation::DailyHistorySongs || operation == ApiOperation::PersonalFm ||
           operation == ApiOperation::MyPlaylists || operation == ApiOperation::LikedSongIds ||
           operation == ApiOperation::SavedAlbums || operation == ApiOperation::SavedArtists ||
           operation == ApiOperation::ListeningStart || operation == ApiOperation::ListeningPlay ||
           operation == ApiOperation::RecentSongs || operation == ApiOperation::ListeningRanking ||
           operation == ApiOperation::RecentPlaylists || operation == ApiOperation::RecentAlbums ||
           operation == ApiOperation::RecentPodcasts || operation == ApiOperation::RecentVoices ||
           operation == ApiOperation::SubscribedPodcasts || operation == ApiOperation::CloudSongs ||
           operation == ApiOperation::CloudSongDetails;
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
    if (isDiscoveryBrowseOperation(operation))
        return discoveryBrowsePayload(operation, parameters);
    if (operation == ApiOperation::SongWikiText) {
        const auto id = parameters.value(QStringLiteral("id"));
        if (parameters.size() != 1 || !id.isString() || !domain::validMusicId(id.toString()))
            return std::nullopt;
        const QJsonObject ext{
            {QStringLiteral("states"),
             QJsonObject{{QStringLiteral("playingResource"),
                          QJsonObject{{QStringLiteral("current"), id},
                                      {QStringLiteral("scene"), QStringLiteral("songWiki")}}}}}};
        return QJsonObject{{QStringLiteral("positionCode"), QStringLiteral("songWikiMainPosition")},
                           {QStringLiteral("extJson"),
                            QString::fromUtf8(QJsonDocument(ext).toJson(QJsonDocument::Compact))}};
    }
    if (isTogetherOperation(operation))
        return togetherPayload(operation, parameters);
    if (isCommentWriteOperation(operation))
        return commentWritePayload(operation, parameters);
    if (isCollectionOperation(operation))
        return collectionOperationPayload(operation, parameters);
    if (isCommentOperation(operation))
        return commentOperationPayload(operation, parameters);
    if (operation == ApiOperation::DailyHistoryDates) {
        if (!parameters.isEmpty())
            return std::nullopt;
        return QJsonObject{};
    }
    if (operation == ApiOperation::DailyHistorySongs) {
        const auto date = parameters.value(QStringLiteral("date"));
        if (parameters.size() != 1 || !date.isString() ||
            !domain::validRecommendationDate(date.toString()))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("date"), date}};
    }
    if (operation == ApiOperation::SearchSuggestions) {
        const auto value = parameters.value(QStringLiteral("keywords"));
        const domain::SearchSuggestionsQuery query{value.toString()};
        if (parameters.size() != 1 || !value.isString() || !query.isValid())
            return std::nullopt;
        return QJsonObject{{QStringLiteral("s"), query.keywords}};
    }
    if (operation == ApiOperation::HotKeywords || operation == ApiOperation::HotKeywordDetails) {
        if (!parameters.isEmpty())
            return std::nullopt;
        return operation == ApiOperation::HotKeywords ? QJsonObject{{QStringLiteral("type"), 1111}}
                                                      : QJsonObject{};
    }
    if (operation == ApiOperation::SubscribedPodcasts || operation == ApiOperation::CloudSongs) {
        const auto limit = parameters.value(QStringLiteral("limit")),
                   offset = parameters.value(QStringLiteral("offset"));
        const domain::CatalogPage page{limit.toInt(-1), offset.toInt(-1)};
        if (parameters.size() != 2 || !page.isValid() || limit != QJsonValue(page.limit) ||
            offset != QJsonValue(page.offset))
            return std::nullopt;
        QJsonObject payload{{QStringLiteral("limit"), page.limit},
                            {QStringLiteral("offset"), page.offset}};
        if (operation == ApiOperation::SubscribedPodcasts)
            payload.insert(QStringLiteral("total"), true);
        return payload;
    }
    if (operation == ApiOperation::CloudSongDetails) {
        const auto raw = parameters.value(QStringLiteral("ids"));
        const auto ids = raw.toArray();
        if (parameters.size() != 1 || !raw.isArray() || ids.isEmpty() || ids.size() > 100)
            return std::nullopt;
        QSet<QString> seen;
        for (const auto v : ids) {
            if (!v.isString() || !domain::validMusicId(v.toString()) || seen.contains(v.toString()))
                return std::nullopt;
            seen.insert(v.toString());
        }
        return QJsonObject{{QStringLiteral("songIds"), ids}}; // actual array, not CSV/JSON text
    }
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
    if (operation == ApiOperation::ArtistIntroduction ||
        operation == ApiOperation::SongWikiSummary) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        if (parameters.size() != 1 || !domain::validMusicId(id))
            return std::nullopt;
        return QJsonObject{{operation == ApiOperation::ArtistIntroduction
                                ? QStringLiteral("id")
                                : QStringLiteral("songId"),
                            id}};
    }
    if (operation == ApiOperation::PodcastDetails || operation == ApiOperation::ProgramDetails) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        if (parameters.size() != 1 || !domain::validMusicId(id))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("id"), id}};
    }
    if (operation == ApiOperation::PodcastPrograms) {
        const auto id = parameters.value(QStringLiteral("radioId")).toString();
        const auto limit = parameters.value(QStringLiteral("limit")),
                   offset = parameters.value(QStringLiteral("offset"));
        const domain::CatalogPage page{limit.toInt(-1), offset.toInt(-1)};
        const auto asc = parameters.value(QStringLiteral("asc"));
        if (parameters.size() != 4 || !domain::validMusicId(id) || !page.isValid() ||
            limit != QJsonValue(page.limit) || offset != QJsonValue(page.offset) || !asc.isBool())
            return std::nullopt;
        return QJsonObject{{QStringLiteral("radioId"), id},
                           {QStringLiteral("limit"), page.limit},
                           {QStringLiteral("offset"), page.offset},
                           {QStringLiteral("asc"), asc}};
    }
    if (operation == ApiOperation::RecentSongs || operation == ApiOperation::RecentPlaylists ||
        operation == ApiOperation::RecentAlbums || operation == ApiOperation::RecentPodcasts ||
        operation == ApiOperation::RecentVoices) {
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
            p.insert(QStringLiteral("total"), true);
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
                           {QStringLiteral("offset"), query.offset},
                           {QStringLiteral("total"), true}};
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
    if (const auto credential = credentialPayload(operation, parameters))
        return *credential;
    if (operation == ApiOperation::SmsCodeSend || operation == ApiOperation::CellphoneLogin ||
        operation == ApiOperation::EmailLogin)
        return std::nullopt;
    QJsonObject payload{{QStringLiteral("type"), 3}};
    switch (operation) {
    case ApiOperation::SmsCodeSend:
    case ApiOperation::CellphoneLogin:
    case ApiOperation::EmailLogin:
        break; // strict credential adapter above
    case ApiOperation::LatestAlbums:
    case ApiOperation::NewAlbums:
    case ApiOperation::AlbumCharts:
    case ApiOperation::ArtistBrowse:
    case ApiOperation::PopularArtists:
    case ApiOperation::ArtistTopSongs:
    case ApiOperation::SimilarSongs:
    case ApiOperation::SimilarArtists:
    case ApiOperation::SimilarPlaylists:
        break; // closed discovery-browse adapter above
    case ApiOperation::TogetherCreate:
    case ApiOperation::TogetherCheck:
    case ApiOperation::TogetherJoin:
    case ApiOperation::TogetherStatus:
    case ApiOperation::TogetherEndCheck:
    case ApiOperation::TogetherEnd:
    case ApiOperation::TogetherHeartbeat:
    case ApiOperation::TogetherPlay:
    case ApiOperation::TogetherQueue:
    case ApiOperation::TogetherReplaceQueue:
    case ApiOperation::TogetherReconnectInfo:
    case ApiOperation::TogetherDeviceNotice:
        break; // closed HTTP-only adapter above
    case ApiOperation::SearchSuggestions:
    case ApiOperation::HotKeywords:
    case ApiOperation::HotKeywordDetails:
        break; // strict closed search-assist adapter above
    case ApiOperation::Charts:
    case ApiOperation::ChartsWithPreviews:
    case ApiOperation::DailyRecommendedPlaylists:
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
    case ApiOperation::DailyHistoryDates:
    case ApiOperation::DailyHistorySongs:
    case ApiOperation::SongComments:
    case ApiOperation::AlbumComments:
    case ApiOperation::PlaylistComments:
    case ApiOperation::ProgramComments:
    case ApiOperation::HotComments:
    case ApiOperation::SortedComments:
    case ApiOperation::CommentFloor:
    case ApiOperation::CommentInfo:
    case ApiOperation::SubscribedPodcasts:
    case ApiOperation::CloudSongs:
    case ApiOperation::CloudSongDetails:
    case ApiOperation::PodcastDetails:
    case ApiOperation::PodcastPrograms:
    case ApiOperation::ProgramDetails:
    case ApiOperation::RecentPlaylists:
    case ApiOperation::RecentAlbums:
    case ApiOperation::RecentPodcasts:
    case ApiOperation::RecentVoices:
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
    case ApiOperation::SongWikiText:
    case ApiOperation::SmsCodeSend:
        return false;
    case ApiOperation::CellphoneLogin:
    case ApiOperation::EmailLogin:
        // Like QR 803: staged for identity verification only, never an account by itself.
        return *response.businessCode == 200;
    case ApiOperation::LatestAlbums:
    case ApiOperation::NewAlbums:
    case ApiOperation::AlbumCharts:
    case ApiOperation::ArtistBrowse:
    case ApiOperation::PopularArtists:
    case ApiOperation::ArtistTopSongs:
    case ApiOperation::SimilarSongs:
    case ApiOperation::SimilarArtists:
    case ApiOperation::SimilarPlaylists:
        return false;
    case ApiOperation::TogetherCreate:
    case ApiOperation::TogetherCheck:
    case ApiOperation::TogetherJoin:
    case ApiOperation::TogetherStatus:
    case ApiOperation::TogetherEndCheck:
    case ApiOperation::TogetherEnd:
    case ApiOperation::TogetherHeartbeat:
    case ApiOperation::TogetherPlay:
    case ApiOperation::TogetherQueue:
    case ApiOperation::TogetherReplaceQueue:
    case ApiOperation::TogetherReconnectInfo:
    case ApiOperation::TogetherDeviceNotice:
        return false;
    case ApiOperation::QrKey:
        return *response.businessCode == 200 &&
               validQrKey(response.object.value(QStringLiteral("unikey")).toString());
    case ApiOperation::QrCheck:
        return *response.businessCode >= 801 && *response.businessCode <= 803;
    case ApiOperation::AccountIdentity:
        return *response.businessCode == 200; // staged only in its isolated request snapshot
    case ApiOperation::SearchSuggestions:
    case ApiOperation::HotKeywords:
    case ApiOperation::HotKeywordDetails:
    case ApiOperation::DailyHistoryDates:
    case ApiOperation::DailyHistorySongs:
    case ApiOperation::SongComments:
    case ApiOperation::AlbumComments:
    case ApiOperation::PlaylistComments:
    case ApiOperation::ProgramComments:
    case ApiOperation::HotComments:
    case ApiOperation::SortedComments:
    case ApiOperation::CommentFloor:
    case ApiOperation::CommentInfo:
    case ApiOperation::SubscribedPodcasts:
    case ApiOperation::CloudSongs:
    case ApiOperation::CloudSongDetails:
    case ApiOperation::Charts:
    case ApiOperation::ChartsWithPreviews:
    case ApiOperation::DailyRecommendedPlaylists:
    case ApiOperation::RecommendedPlaylists:
    case ApiOperation::PlaylistList:
    case ApiOperation::PremiumPlaylists:
    case ApiOperation::PlaylistCatalogue:
    case ApiOperation::HotPlaylistTags:
    case ApiOperation::PremiumPlaylistTags:
    case ApiOperation::PodcastDetails:
    case ApiOperation::PodcastPrograms:
    case ApiOperation::ProgramDetails:
    case ApiOperation::RecentPlaylists:
    case ApiOperation::RecentAlbums:
    case ApiOperation::RecentPodcasts:
    case ApiOperation::RecentVoices:
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
