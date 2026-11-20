#include "catalog_mapper.h"
#include "metadata_mapping.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <QSet>
#include <QVariant>
#include <cmath>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }
// Which check a response failed; the gateway logs the failure itself (music.invalid).
MusicFailure invalidBecause(const char* reason, qsizetype row = -1)
{
    rejectedBecause(reason, row);
    return MusicFailure{MusicError::InvalidResponse};
}
bool description(const QJsonValue& v)
{
    return absent(v) || (v.isString() && v.toString().size() <= 65536 &&
                         v.toString().isValidUtf16() && !v.toString().contains(QChar::Null));
}
bool number(const QJsonObject& o, const QString& key, std::optional<qint64>& out,
            bool timestamp = false)
{
    const auto v = o.value(key);
    if (absent(v)) {
        out.reset();
        return true;
    }
    if (!timestamp) {
        out = nonnegative(v);
        return out.has_value();
    }
    if (v.toVariant().metaType().id() != QMetaType::LongLong)
        return false;
    const auto n = v.toInteger();
    if (n < -9007199254740991LL || n > 9007199254740991LL)
        return false;
    out = n;
    return true; // pre-1970 album dates are valid
}
QUrl image(const QJsonValue& value)
{
    const auto text = value.toString();
    const QUrl url(text, QUrl::StrictMode);
    if (text.size() > 2048 || !url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        url.hasFragment() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
        return {};
    return url; // metadata only, not authorization to fetch arbitrary images
}
std::optional<QList<ArtistCredit>> credits(const QJsonValue& value)
{
    if (absent(value))
        return QList<ArtistCredit>{};
    if (!value.isArray() || value.toArray().size() > 100)
        return std::nullopt;
    QList<ArtistCredit> list;
    for (const auto v : value.toArray()) {
        if (!v.isObject())
            return std::nullopt;
        const auto o = v.toObject();
        const auto raw = o.value(QStringLiteral("id"));
        const auto id = providerId(raw);
        if ((!unknownId(raw) && id.isEmpty()) || !textField(o.value(QStringLiteral("name"))))
            return std::nullopt;
        list.append({id.isEmpty() ? std::nullopt : std::optional<ArtistId>(ArtistId{id}),
                     o.value(QStringLiteral("name")).toString()});
    }
    return list;
}
std::optional<Album> album(const QJsonObject& o)
{
    Album a;
    a.id = {providerId(o.value(QStringLiteral("id")))};
    if (a.id.value.isEmpty() || !textField(o.value(QStringLiteral("name"))) ||
        !description(o.value(QStringLiteral("description"))) ||
        !number(o, QStringLiteral("size"), a.trackCount) ||
        !number(o, QStringLiteral("publishTime"), a.publishedAtMs, true) ||
        !metadataList(o.value(QStringLiteral("alias")), a.aliases) ||
        !metadataText(o.value(QStringLiteral("company")), a.company) ||
        !metadataText(o.value(QStringLiteral("type")), a.type) ||
        !metadataText(o.value(QStringLiteral("subType")), a.subType))
        return std::nullopt;
    auto raw = o.value(QStringLiteral("artists"));
    if (absent(raw)) {
        const auto single = o.value(QStringLiteral("artist"));
        if (!absent(single) && !single.isObject())
            return std::nullopt;
        if (single.isObject())
            raw = QJsonArray{single};
    }
    const auto artists = credits(raw);
    if (!artists)
        return std::nullopt;
    a.artists = *artists;
    a.title = o.value(QStringLiteral("name")).toString();
    a.description = o.value(QStringLiteral("description")).toString();
    a.cover = image(o.value(QStringLiteral("picUrl")));
    return a;
}
std::optional<QList<Song>> songs(const QJsonValue& v, qsizetype maximum)
{
    if (!v.isArray() || v.toArray().size() > maximum)
        return std::nullopt;
    QList<Song> list;
    for (const auto row : v.toArray()) {
        if (!row.isObject())
            return std::nullopt;
        const auto song = songFrom(row.toObject());
        if (!song)
            return std::nullopt;
        list.append(*song);
    }
    return list;
}
bool pagination(const QJsonObject& o, std::optional<qint64>& total, std::optional<bool>& more)
{
    if (!number(o, QStringLiteral("total"), total))
        return false;
    const auto v = o.value(QStringLiteral("more"));
    if (!absent(v)) {
        if (!v.isBool())
            return false;
        more = v.toBool();
    }
    return true;
}
bool artistMatches(const QJsonValue& value, const ArtistId& expected)
{
    return absent(value) ||
           (value.isObject() &&
            providerId(value.toObject().value(QStringLiteral("id"))) == expected.value);
}
QJsonObject idParameter(const QString& id) { return {{QStringLiteral("id"), id}}; }
QJsonObject pageParameters(const QString& id, CatalogPage page)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("limit"), page.limit},
            {QStringLiteral("offset"), page.offset}};
}
} // namespace
std::optional<Album> mapping::albumFrom(const QJsonObject& object) { return album(object); }
QUrl mapping::imageFrom(const QJsonValue& value) { return image(value); }
std::optional<CatalogRequest> catalogRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<ArtistIntroductionQuery>(&query))
        return CatalogRequest{ApiOperation::ArtistIntroduction, idParameter(q->artist.value)};
    if (const auto* q = std::get_if<SongWikiQuery>(&query))
        return CatalogRequest{ApiOperation::SongWikiSummary, idParameter(q->song.value)};
    if (const auto* q = std::get_if<AlbumQuery>(&query))
        return CatalogRequest{ApiOperation::AlbumDetails, idParameter(q->album.value)};
    if (const auto* q = std::get_if<ArtistQuery>(&query))
        return CatalogRequest{ApiOperation::ArtistDetails, idParameter(q->artist.value)};
    if (const auto* q = std::get_if<PlaylistQuery>(&query))
        return CatalogRequest{ApiOperation::PlaylistDetails, idParameter(q->playlist.value)};
    if (const auto* q = std::get_if<PlaylistSongsQuery>(&query))
        return CatalogRequest{ApiOperation::PlaylistDetails, idParameter(q->playlist.value)};
    if (const auto* q = std::get_if<ArtistAlbumsQuery>(&query))
        return CatalogRequest{ApiOperation::ArtistAlbums, pageParameters(q->artist.value, q->page)};
    if (const auto* q = std::get_if<ArtistSongsQuery>(&query)) {
        auto p = pageParameters(q->artist.value, q->page);
        p.insert(QStringLiteral("order"),
                 q->order == ArtistSongOrder::Hot ? QStringLiteral("hot") : QStringLiteral("time"));
        return CatalogRequest{ApiOperation::ArtistSongs, p};
    }
    return std::nullopt;
}
MusicResult mapCatalogResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!validMusicQuery(query) || object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (const auto* q = std::get_if<AlbumQuery>(&query)) {
        const auto a = album(object.value(QStringLiteral("album")).toObject());
        const auto rows = songs(object.value(QStringLiteral("songs")), 10000);
        if (!a || a->id.value != q->album.value || !rows ||
            (a->trackCount && *a->trackCount < rows->size()))
            return invalid;
        return AlbumDetails{*a, *rows, a->trackCount == std::optional<qint64>(rows->size())};
    }
    if (const auto* q = std::get_if<ArtistQuery>(&query)) {
        const auto o = object.value(QStringLiteral("data"))
                           .toObject()
                           .value(QStringLiteral("artist"))
                           .toObject();
        ArtistDetails a;
        a.id = {providerId(o.value(QStringLiteral("id")))};
        if (a.id.value != q->artist.value || !textField(o.value(QStringLiteral("name"))) ||
            !description(o.value(QStringLiteral("briefDesc"))) ||
            !number(o, QStringLiteral("musicSize"), a.songCount) ||
            !number(o, QStringLiteral("albumSize"), a.albumCount) ||
            !metadataList(o.value(QStringLiteral("alias")), a.aliases) ||
            !metadataList(o.value(QStringLiteral("transNames")), a.translatedNames))
            return invalid;
        a.name = o.value(QStringLiteral("name")).toString();
        a.description = o.value(QStringLiteral("briefDesc")).toString();
        a.portrait = image(o.contains(QStringLiteral("cover")) ? o.value(QStringLiteral("cover"))
                                                               : o.value(QStringLiteral("avatar")));
        // data.user (measured 2026-11-10): the musician's account; absent for others.
        const auto user =
            object.value(QStringLiteral("data")).toObject().value(QStringLiteral("user"));
        if (user.isObject()) {
            const auto raw = user.toObject().value(QStringLiteral("userId"));
            if (!unknownId(raw)) {
                const auto id = providerId(raw);
                if (id.isEmpty())
                    return invalid;
                a.userId = id;
            }
        } else if (!user.isNull() && !user.isUndefined())
            return invalid;
        return a;
    }
    if (const auto* q = std::get_if<ArtistSongsQuery>(&query)) {
        ArtistSongsPage page;
        page.query = *q;
        const auto rows = songs(object.value(QStringLiteral("songs")), q->page.limit);
        if (!rows || !artistMatches(object.value(QStringLiteral("artist")), q->artist) ||
            !pagination(object, page.total, page.hasMore))
            return invalid;
        page.songs = *rows;
        return page;
    }
    if (const auto* q = std::get_if<ArtistAlbumsQuery>(&query)) {
        ArtistAlbumsPage page;
        page.query = *q;
        const auto rows = object.value(QStringLiteral("hotAlbums"));
        if (!rows.isArray() || rows.toArray().size() > q->page.limit ||
            !artistMatches(object.value(QStringLiteral("artist")), q->artist) ||
            !pagination(object, page.total, page.hasMore))
            return invalid;
        if (!page.total && !number(object.value(QStringLiteral("artist")).toObject(),
                                   QStringLiteral("albumSize"), page.total))
            return invalid;
        for (const auto v : rows.toArray()) {
            if (!v.isObject())
                return invalid;
            const auto a = album(v.toObject());
            if (!a)
                return invalid;
            page.albums.append(*a);
        }
        return page;
    }
    if (const auto* q = std::get_if<PlaylistQuery>(&query)) {
        const auto o = object.value(QStringLiteral("playlist")).toObject();
        PlaylistDetails p;
        p.id = {providerId(o.value(QStringLiteral("id")))};
        if (p.id.value != q->playlist.value)
            return invalidBecause("playlist_id");
        if (const char* field =
                !textField(o.value(QStringLiteral("name")))               ? "name"
                : !description(o.value(QStringLiteral("description")))    ? "description"
                : !number(o, QStringLiteral("trackCount"), p.trackCount)  ? "track_count"
                : !number(o, QStringLiteral("updateTime"), p.updatedAtMs) ? "update_time"
                : !metadataList(o.value(QStringLiteral("tags")), p.tags)  ? "tags"
                : !number(o, QStringLiteral("createTime"), p.createdAtMs) ? "create_time"
                : !number(o, QStringLiteral("subscribedCount"), p.subscriberCount)
                    ? "subscribed_count"
                : !number(o, QStringLiteral("commentCount"), p.commentCount) ? "comment_count"
                : !number(o, QStringLiteral("shareCount"), p.shareCount)     ? "share_count"
                                                                             : nullptr)
            return invalidBecause(field);
        p.title = o.value(QStringLiteral("name")).toString();
        p.description = o.value(QStringLiteral("description")).toString();
        p.cover = image(o.value(QStringLiteral("coverImgUrl")));
        const auto count = o.value(QStringLiteral("playCount"));
        if (!absent(count)) {
            if (!count.isDouble() || !std::isfinite(count.toDouble()) || count.toDouble() < 0 ||
                count.toDouble() > 9007199254740991.0)
                return invalidBecause("play_count");
            p.playCount = count.toDouble();
        }
        const auto creator = o.value(QStringLiteral("creator"));
        if (!absent(creator)) {
            if (!creator.isObject())
                return invalidBecause("creator");
            const auto user = creator.toObject();
            const auto raw = user.value(QStringLiteral("userId"));
            const auto id = providerId(raw);
            if ((!unknownId(raw) && id.isEmpty()) ||
                !textField(user.value(QStringLiteral("nickname"))))
                return invalidBecause("creator");
            p.creator = PlaylistCreator{id.isEmpty() ? std::nullopt : std::optional<QString>(id),
                                        user.value(QStringLiteral("nickname")).toString()};
        }
        const auto ids = o.value(QStringLiteral("trackIds"));
        QSet<QString> known;
        if (!absent(ids)) {
            if (!ids.isArray() || ids.toArray().size() > 100000)
                return invalidBecause("track_ids");
            p.trackIds = QList<SongId>{};
            const auto index = ids.toArray();
            for (qsizetype i = 0; i < index.size(); ++i) {
                if (!index.at(i).isObject())
                    return invalidBecause("track_id", i);
                const auto id = providerId(index.at(i).toObject().value(QStringLiteral("id")));
                if (id.isEmpty())
                    return invalidBecause("track_id", i);
                p.trackIds->append({id});
                known.insert(id);
            }
            if (p.trackCount && *p.trackCount < p.trackIds->size())
                return invalidBecause("track_count_below_index");
            p.trackIdsComplete = p.trackCount == std::optional<qint64>(p.trackIds->size());
        }
        // The embedded tracks are some songs' metadata sent along, not the index (trackIds):
        // a row we cannot read is left out, its song still listed and later asked for by ID.
        // One odd song (a cloud upload's tags, say) must not hide the whole playlist.
        const auto rows = o.value(QStringLiteral("tracks"));
        if (!absent(rows) && (!rows.isArray() || rows.toArray().size() > 10000))
            return invalidBecause("tracks");
        const auto embedded = rows.toArray();
        qsizetype skipped = 0, firstSkipped = -1;
        const char* skippedBecause = "track";
        for (qsizetype i = 0; i < embedded.size(); ++i) {
            const char* why = "track";
            const auto song = embedded.at(i).isObject()
                                  ? songFromExplained(embedded.at(i).toObject(), why)
                                  : std::optional<Song>{};
            if (!song) {
                if (skipped++ == 0) {
                    firstSkipped = i;
                    skippedBecause = why;
                }
                continue;
            }
            if (p.trackIds && !known.contains(song->id.value))
                return invalidBecause("unsolicited_track", i);
            p.songs.append(*song);
        }
        if (skipped) {
            const auto song =
                providerId(embedded.at(firstSkipped).toObject().value(QStringLiteral("id")));
            log::info(lcApi, "playlist.tracks_skipped",
                      {{"playlist", p.id.value},
                       {"count", skipped},
                       {"row", firstSkipped},
                       {"song", song.isEmpty() ? QStringLiteral("-") : song},
                       {"reason", skippedBecause}});
        }
        return p;
    }
    return invalid; // PlaylistSongsQuery is a bounded two-stage gateway operation, not a raw schema
}
} // namespace cirrus::netease
