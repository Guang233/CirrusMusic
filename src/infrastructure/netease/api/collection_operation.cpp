// Fixed api-enhanced like/playlist_tracks/album_sub/artist_sub/dj_sub (MIT); see NOTICE.
#include "collection_operation.h"
#include "domain/music/music_types.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
namespace cirrus::netease
{
bool isCollectionOperation(ApiOperation op)
{
    switch (op) {
    case ApiOperation::SongHeart:
    case ApiOperation::PlaylistCollectSongs:
    case ApiOperation::AlbumSubscribe:
    case ApiOperation::AlbumUnsubscribe:
    case ApiOperation::ArtistSubscribe:
    case ApiOperation::ArtistUnsubscribe:
    case ApiOperation::PodcastSubscribe:
    case ApiOperation::PodcastUnsubscribe:
        return true;
    default:
        return false;
    }
}
std::optional<ApiOperationContract> collectionOperationContract(ApiOperation op)
{
    QString path;
    auto protocol = Protocol::Weapi;
    switch (op) {
    case ApiOperation::SongHeart:
        path = QStringLiteral("/api/radio/like");
        break;
    case ApiOperation::PlaylistCollectSongs:
        path = QStringLiteral("/api/playlist/manipulate/tracks");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::AlbumSubscribe:
        path = QStringLiteral("/api/album/sub");
        break;
    case ApiOperation::AlbumUnsubscribe:
        path = QStringLiteral("/api/album/unsub");
        break;
    case ApiOperation::ArtistSubscribe:
        path = QStringLiteral("/api/artist/sub");
        break;
    case ApiOperation::ArtistUnsubscribe:
        path = QStringLiteral("/api/artist/unsub");
        break;
    case ApiOperation::PodcastSubscribe:
        path = QStringLiteral("/api/djradio/sub");
        break;
    case ApiOperation::PodcastUnsubscribe:
        path = QStringLiteral("/api/djradio/unsub");
        break;
    default:
        return std::nullopt;
    }
    return ApiOperationContract{path, protocol, ResponseEncoding::Json, 64 * 1024};
}
std::optional<QJsonObject> collectionOperationPayload(ApiOperation op, const QJsonObject& p)
{
    if (!isCollectionOperation(op))
        return std::nullopt;
    if (op == ApiOperation::PlaylistCollectSongs) {
        const auto pid = p.value(QStringLiteral("pid"));
        const auto action = p.value(QStringLiteral("op")).toString();
        const auto raw = p.value(QStringLiteral("ids"));
        if (p.size() != 3 || !pid.isString() || !domain::validMusicId(pid.toString()) ||
            (action != QStringLiteral("add") && action != QStringLiteral("del")) ||
            !raw.isArray() || raw.toArray().isEmpty() || raw.toArray().size() > 100)
            return std::nullopt;
        QSet<QString> seen;
        for (const auto id : raw.toArray()) {
            if (!id.isString() || !domain::validMusicId(id.toString()) ||
                seen.contains(id.toString()))
                return std::nullopt;
            seen.insert(id.toString());
        }
        return QJsonObject{
            {QStringLiteral("pid"), pid},
            {QStringLiteral("op"), action},
            {QStringLiteral("trackIds"),
             QString::fromUtf8(QJsonDocument(raw.toArray()).toJson(QJsonDocument::Compact))},
            {QStringLiteral("imme"), QStringLiteral("true")}};
    }
    const auto id = p.value(QStringLiteral("id"));
    if (!id.isString() || !domain::validMusicId(id.toString()))
        return std::nullopt;
    if (op == ApiOperation::SongHeart) {
        const auto liked = p.value(QStringLiteral("liked"));
        if (p.size() != 2 || !liked.isBool())
            return std::nullopt;
        return QJsonObject{{QStringLiteral("trackId"), id},
                           {QStringLiteral("like"), liked},
                           {QStringLiteral("alg"), QStringLiteral("itembased")},
                           {QStringLiteral("time"), QStringLiteral("3")}};
    }
    if (p.size() != 1)
        return std::nullopt;
    if (op == ApiOperation::ArtistSubscribe || op == ApiOperation::ArtistUnsubscribe)
        // Pinned numeric JSON text, NOT a JSON double or fabricated string-array variant.
        return QJsonObject{{QStringLiteral("artistId"), id},
                           {QStringLiteral("artistIds"),
                            QStringLiteral("[") + id.toString() + QStringLiteral("]")}};
    return QJsonObject{{QStringLiteral("id"), id}};
}
} // namespace cirrus::netease
