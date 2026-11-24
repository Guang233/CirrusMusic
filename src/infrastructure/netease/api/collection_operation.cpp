// Fixed api-enhanced collection/playlist write wrappers (MIT); see NOTICE. Playlist
// subscription follows the official Android app 9.6.22 (docs/api/playlist-subscription.md);
// cloud drive writes follow the app and the fixed reference (docs/api/cloud-drive.md).
#include "collection_operation.h"
#include "domain/music/collection_types.h"
#include "domain/music/playlist_tag_edit_types.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
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
    case ApiOperation::PlaylistSubscribe:
    case ApiOperation::PlaylistUnsubscribe:
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
    case ApiOperation::CloudDelete:
    case ApiOperation::CloudMatch:
    case ApiOperation::CloudUploadCheck:
    case ApiOperation::CloudUploadAllocate:
    case ApiOperation::CloudUploadInfo:
    case ApiOperation::CloudPublish:
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
    case ApiOperation::PlaylistSubscribe:
        path = QStringLiteral("/api/playlist/subscribe");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::PlaylistUnsubscribe:
        path = QStringLiteral("/api/playlist/unsubscribe");
        protocol = Protocol::Eapi;
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
    case ApiOperation::PlaylistTagsUpdate:
        path = QStringLiteral("/api/playlist/tags/update");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::PlaylistMakePublic:
        path = QStringLiteral("/api/playlist/update/privacy");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::CoverAllocate:
        path = QStringLiteral("/api/nos/token/alloc");
        break;
    case ApiOperation::PlaylistCoverCommit:
        path = QStringLiteral("/api/playlist/cover/update");
        break;
    case ApiOperation::CloudDelete:
        path = QStringLiteral("/api/cloud/del");
        break;
    case ApiOperation::CloudMatch:
        path = QStringLiteral("/api/cloud/user/song/match");
        break;
    case ApiOperation::CloudUploadCheck:
        path = QStringLiteral("/api/cloud/upload/check");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::CloudUploadAllocate:
        path = QStringLiteral("/api/nos/token/alloc");
        break;
    case ApiOperation::CloudUploadInfo:
        path = QStringLiteral("/api/upload/cloud/info/v2");
        protocol = Protocol::Eapi;
        break;
    case ApiOperation::CloudPublish:
        path = QStringLiteral("/api/cloud/pub/v2");
        protocol = Protocol::Eapi;
        break;
    default:
        return std::nullopt;
    }
    return ApiOperationContract{path, protocol, ResponseEncoding::Json, 64 * 1024};
}
namespace
{
bool hexMd5(const QJsonValue& v)
{
    static const QRegularExpression md5(QStringLiteral("\\A[0-9a-f]{32}\\z"));
    return v.isString() && md5.match(v.toString()).hasMatch();
}
bool digits(const QJsonValue& v) { return v.isString() && domain::validMusicId(v.toString()); }
// Display text the user's file gave (title, artist, album, file name): bounded, no controls.
bool fileText(const QJsonValue& v, qsizetype maximum)
{
    return v.isString() && !v.toString().trimmed().isEmpty() &&
           domain::validPlaylistText(v.toString(), maximum, false);
}
std::optional<QJsonObject> cloudPayload(ApiOperation op, const QJsonObject& p)
{
    const auto has = [&p](std::initializer_list<const char*> names) {
        if (p.size() != qsizetype(names.size()))
            return false;
        for (const char* name : names)
            if (!p.contains(QLatin1StringView(name)))
                return false;
        return true;
    };
    switch (op) {
    case ApiOperation::CloudDelete: {
        // As the app sends it: a JSON array of numbers as text (k41/d.java).
        const auto raw = p.value(QStringLiteral("songIds"));
        if (!has({"songIds"}) || !raw.isArray() || raw.toArray().isEmpty() ||
            raw.toArray().size() > domain::CloudSongDelete::MaximumSongs)
            return std::nullopt;
        QSet<QString> seen;
        QStringList ids;
        for (const auto id : raw.toArray()) {
            if (!digits(id) || seen.contains(id.toString()))
                return std::nullopt;
            seen.insert(id.toString());
            ids.append(id.toString());
        }
        return QJsonObject{{QStringLiteral("songIds"),
                            QStringLiteral("[") + ids.join(u',') + QStringLiteral("]")}};
    }
    case ApiOperation::CloudMatch: {
        const auto target = p.value(QStringLiteral("adjustSongId"));
        if (!has({"userId", "songId", "adjustSongId"}) ||
            !digits(p.value(QStringLiteral("userId"))) ||
            !digits(p.value(QStringLiteral("songId"))) || !target.isString() ||
            (target.toString() != QStringLiteral("0") && !digits(target)))
            return std::nullopt;
        return p;
    }
    case ApiOperation::CloudUploadCheck: {
        const auto length = p.value(QStringLiteral("length"));
        if (!has({"md5", "length", "bitrate"}) || !hexMd5(p.value(QStringLiteral("md5"))) ||
            !digits(length) || length.toString().size() > 12 ||
            !digits(p.value(QStringLiteral("bitrate"))))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("bitrate"), p.value(QStringLiteral("bitrate"))},
                           {QStringLiteral("ext"), QString()},
                           {QStringLiteral("length"), length.toString().toLongLong()},
                           {QStringLiteral("md5"), p.value(QStringLiteral("md5"))},
                           {QStringLiteral("songId"), QStringLiteral("0")},
                           {QStringLiteral("version"), 1}};
    }
    case ApiOperation::CloudUploadAllocate: {
        static const QRegularExpression ext(QStringLiteral("\\A[a-z0-9]{1,8}\\z"));
        const auto e = p.value(QStringLiteral("ext"));
        if (!has({"md5", "ext", "filename"}) || !hexMd5(p.value(QStringLiteral("md5"))) ||
            !e.isString() || !ext.match(e.toString()).hasMatch() ||
            !fileText(p.value(QStringLiteral("filename")), 255))
            return std::nullopt;
        return QJsonObject{
            {QStringLiteral("bucket"), QStringLiteral("jd-musicrep-privatecloud-audio-public")},
            {QStringLiteral("ext"), e},
            {QStringLiteral("filename"), p.value(QStringLiteral("filename"))},
            {QStringLiteral("local"), false},
            {QStringLiteral("nos_product"), 3},
            {QStringLiteral("type"), QStringLiteral("audio")},
            {QStringLiteral("md5"), p.value(QStringLiteral("md5"))}};
    }
    case ApiOperation::CloudUploadInfo:
        if (!has({"md5", "songid", "filename", "song", "album", "artist", "bitrate",
                  "resourceId"}) ||
            !hexMd5(p.value(QStringLiteral("md5"))) || !digits(p.value(QStringLiteral("songid"))) ||
            !fileText(p.value(QStringLiteral("filename")), 255) ||
            !fileText(p.value(QStringLiteral("song")), 256) ||
            !fileText(p.value(QStringLiteral("album")), 256) ||
            !fileText(p.value(QStringLiteral("artist")), 256) ||
            !digits(p.value(QStringLiteral("bitrate"))) ||
            !digits(p.value(QStringLiteral("resourceId"))))
            return std::nullopt;
        return p;
    case ApiOperation::CloudPublish:
        if (!has({"songid"}) || !digits(p.value(QStringLiteral("songid"))))
            return std::nullopt;
        return p;
    default:
        return std::nullopt;
    }
}
} // namespace
std::optional<QJsonObject> collectionOperationPayload(ApiOperation op, const QJsonObject& p)
{
    if (!isCollectionOperation(op))
        return std::nullopt;
    if (op == ApiOperation::CloudDelete || op == ApiOperation::CloudMatch ||
        op == ApiOperation::CloudUploadCheck || op == ApiOperation::CloudUploadAllocate ||
        op == ApiOperation::CloudUploadInfo || op == ApiOperation::CloudPublish)
        return cloudPayload(op, p);
    if (op == ApiOperation::CoverAllocate) {
        if (!p.isEmpty())
            return std::nullopt;
        return QJsonObject{{QStringLiteral("bucket"), QStringLiteral("yyimgs")},
                           {QStringLiteral("ext"), QStringLiteral("jpg")},
                           {QStringLiteral("filename"), QStringLiteral("cirrus-cover.jpg")},
                           {QStringLiteral("local"), false},
                           {QStringLiteral("nos_product"), 0},
                           {QStringLiteral("return_body"),
                            QStringLiteral("{\"code\":200,\"size\":\"$(ObjectSize)\"}")},
                           {QStringLiteral("type"), QStringLiteral("other")}};
    }
    if (op == ApiOperation::PlaylistCoverCommit) {
        if (p.size() != 2 || !p.value(QStringLiteral("id")).isString() ||
            !p.value(QStringLiteral("coverImgId")).isString() ||
            !domain::validMusicId(p.value(QStringLiteral("id")).toString()) ||
            !domain::validMusicId(p.value(QStringLiteral("coverImgId")).toString()))
            return std::nullopt;
        return p;
    }
    if (op == ApiOperation::PlaylistTagsUpdate || op == ApiOperation::PlaylistMakePublic) {
        const auto id = p.value(QStringLiteral("id"));
        const bool tags = op == ApiOperation::PlaylistTagsUpdate;
        if (!id.isString() || !domain::validMusicId(id.toString()) || p.size() != (tags ? 2 : 1))
            return std::nullopt;
        if (!tags)
            return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("privacy"), 0}};
        const auto raw = p.value(QStringLiteral("tags"));
        if (!raw.isArray() || raw.toArray().isEmpty() || raw.toArray().size() > 3)
            return std::nullopt;
        QStringList selected;
        for (const auto tag : raw.toArray()) {
            if (!tag.isString())
                return std::nullopt;
            selected.append(tag.toString());
        }
        if (!domain::validEditablePlaylistTags(selected))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("tags"), selected.join(u';')}};
    }
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
    if (op == ApiOperation::PlaylistSubscribe)
        // The app sends its Yidun token, "" when the SDK has none (by0.b.G0()); Cirrus has
        // no SDK and sends what the app does then (the maintainer's choice, 2026-11-22).
        return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("checkToken"), QString()}};
    if (op == ApiOperation::ArtistSubscribe || op == ApiOperation::ArtistUnsubscribe)
        // Pinned numeric JSON text, NOT a JSON double or fabricated string-array variant.
        return QJsonObject{{QStringLiteral("artistId"), id},
                           {QStringLiteral("artistIds"),
                            QStringLiteral("[") + id.toString() + QStringLiteral("]")}};
    return QJsonObject{{QStringLiteral("id"), id}};
}
} // namespace cirrus::netease
