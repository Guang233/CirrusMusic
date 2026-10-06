// Fixed api-enhanced collection/playlist create/name/desc wrappers (MIT); see NOTICE.
#include "collection_operation.h"
#include "domain/music/collection_types.h"
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
    case ApiOperation::PlaylistCreate:
    case ApiOperation::PlaylistRename:
    case ApiOperation::PlaylistDescription:
    case ApiOperation::PlaylistDelete:
    case ApiOperation::PlaylistSongOrder:
    case ApiOperation::AccountPlaylistOrder:
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
    case ApiOperation::PlaylistCreate:
        path = QStringLiteral("/api/playlist/create");
        break;
    case ApiOperation::PlaylistRename:
        path = QStringLiteral("/api/playlist/update/name");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::PlaylistDescription:
        path = QStringLiteral("/api/playlist/desc/update");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::PlaylistDelete:
        path = QStringLiteral("/api/playlist/remove");
        break;
    case ApiOperation::PlaylistSongOrder:
        path = QStringLiteral("/api/playlist/manipulate/tracks");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::AccountPlaylistOrder:
        path = QStringLiteral("/api/playlist/order/update");
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
    if (op == ApiOperation::PlaylistDelete) {
        const auto id = p.value(QStringLiteral("id"));
        if (p.size() != 1 || !id.isString() || !domain::validMusicId(id.toString()))
            return std::nullopt;
        return QJsonObject{
            {QStringLiteral("ids"), QStringLiteral("[") + id.toString() + QStringLiteral("]")}};
    }
    if (op == ApiOperation::PlaylistSongOrder || op == ApiOperation::AccountPlaylistOrder) {
        const bool songs = op == ApiOperation::PlaylistSongOrder;
        const auto raw = p.value(QStringLiteral("ids"));
        const auto pid = p.value(QStringLiteral("pid"));
        if (p.size() != (songs ? 2 : 1) || !raw.isArray() || raw.toArray().isEmpty() ||
            raw.toArray().size() > (songs ? domain::PlaylistSongOrder::MaximumIds
                                          : domain::AccountPlaylistOrder::MaximumIds) ||
            (songs && (!pid.isString() || !domain::validMusicId(pid.toString()))))
            return std::nullopt;
        QSet<QString> seen;
        QStringList ids;
        for (const auto id : raw.toArray()) {
            if (!id.isString() || !domain::validMusicId(id.toString()) ||
                seen.contains(id.toString()))
                return std::nullopt;
            seen.insert(id.toString());
            ids.append(id.toString());
        }
        const auto text = QStringLiteral("[") + ids.join(u',') + QStringLiteral("]");
        if (songs)
            return QJsonObject{{QStringLiteral("pid"), pid},
                               {QStringLiteral("trackIds"), text},
                               {QStringLiteral("op"), QStringLiteral("update")}};
        return QJsonObject{{QStringLiteral("ids"), text}};
    }
    if (op == ApiOperation::PlaylistCreate) {
        const auto name = p.value(QStringLiteral("name"));
        const auto privacy = p.value(QStringLiteral("privacy"));
        if (p.size() != 2 || !name.isString() ||
            !domain::validPlaylistText(name.toString(), 128, false) || !privacy.isString() ||
            (privacy.toString() != QStringLiteral("0") &&
             privacy.toString() != QStringLiteral("10")))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("name"), name},
                           {QStringLiteral("privacy"), privacy},
                           {QStringLiteral("type"), QStringLiteral("NORMAL")}};
    }
    if (op == ApiOperation::PlaylistRename || op == ApiOperation::PlaylistDescription) {
        const auto id = p.value(QStringLiteral("id"));
        const bool description = op == ApiOperation::PlaylistDescription;
        const auto key = description ? QStringLiteral("desc") : QStringLiteral("name");
        const auto text = p.value(key);
        if (p.size() != 2 || !id.isString() || !domain::validMusicId(id.toString()) ||
            !text.isString() ||
            !domain::validPlaylistText(text.toString(), description ? 4096 : 128, description))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("id"), id}, {key, text}};
    }
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
