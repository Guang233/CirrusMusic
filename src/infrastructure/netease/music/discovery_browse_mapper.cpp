#include "discovery_browse_mapper.h"
#include "music_mapping.h"
#include "spoken_mapping.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace cirrus::netease
{
namespace
{
using namespace domain;
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }
bool requiredName(const QJsonObject& o)
{
    const auto v = o.value(QStringLiteral("name"));
    return v.isString() && textField(v);
}
bool optionalCount(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
}
bool optionalMore(const QJsonValue& v, std::optional<bool>& out)
{
    if (absent(v))
        return true;
    if (!v.isBool())
        return false;
    out = v.toBool();
    return true;
}
std::optional<SearchArtist> artist(const QJsonObject& o)
{
    SearchArtist a;
    a.id = {providerId(o.value(QStringLiteral("id")))};
    if (a.id.value.isEmpty() || !requiredName(o) ||
        !optionalCount(o.value(QStringLiteral("albumSize")), a.albumCount) ||
        !optionalCount(o.value(QStringLiteral("musicSize")), a.songCount))
        return std::nullopt;
    a.name = o.value(QStringLiteral("name")).toString();
    a.portrait = imageFrom(o.value(QStringLiteral("picUrl")));
    return a;
}
template <typename T, typename Mapper>
bool list(const QJsonValue& v, int limit, QList<T>& out, Mapper mapper)
{
    if (!v.isArray() || v.toArray().size() > limit)
        return false;
    for (const auto value : v.toArray()) {
        if (!value.isObject() || !requiredName(value.toObject()))
            return false;
        const auto row = mapper(value.toObject());
        if (!row)
            return false;
        out.append(*row);
    }
    return true; // preserve order/duplicates, no automatic detail or fetch
}
} // namespace
application::MusicResult mapDiscoveryBrowseResponse(const domain::DiscoveryBrowseQuery& query,
                                                    const QJsonObject& object)
{
    using namespace application;
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!query.isValid() || object.value(QStringLiteral("code")) != QJsonValue(200) ||
        QJsonDocument(object).toJson(QJsonDocument::Compact).size() > 2 * 1024 * 1024)
        return invalid;
    DiscoveryBrowsePage result;
    result.query = query;
    const auto kind = query.action.index();
    const auto maxRows = std::visit(
        [](const auto& q) {
            if constexpr (requires { q.page; })
                return q.page.limit;
            return 100;
        },
        query.action);
    if (kind == 2) {
        DiscoveryAlbumCharts groups;
        const auto group = [&](QString key, std::optional<QList<Album>>& out) {
            if (!object.contains(key))
                return true;
            QList<Album> rows;
            if (!list(object.value(key), 500, rows, albumFrom))
                return false;
            out = std::move(rows);
            return true;
        };
        if (!group(QStringLiteral("weekData"), groups.week) ||
            !group(QStringLiteral("monthData"), groups.month) || (!groups.week && !groups.month) ||
            !optionalMore(object.value(QStringLiteral("hasMore")), result.hasMore))
            return invalid;
        result.content = std::move(groups);
        return result;
    }
    if (kind <= 1) {
        DiscoveryAlbums albums;
        if (!list(object.value(QStringLiteral("albums")), maxRows, albums.albums, albumFrom))
            return invalid;
        if (kind == 1 && (!optionalCount(object.value(QStringLiteral("total")), result.total) ||
                          !optionalMore(object.value(QStringLiteral("hasMore")), result.hasMore) ||
                          (result.total && *result.total < albums.albums.size())))
            return invalid;
        result.content = std::move(albums);
    } else if (kind == 3 || kind == 4 || kind == 7) {
        DiscoveryArtists artists;
        if (!list(object.value(QStringLiteral("artists")), maxRows, artists.artists, artist) ||
            ((kind == 3 || kind == 4) &&
             !optionalMore(object.value(QStringLiteral("more")), result.hasMore)))
            return invalid;
        result.content = std::move(artists);
    } else if (kind == 5 || kind == 6) {
        DiscoverySongs songs;
        // Observed SimilarSongs returns a batch larger than requested limit; never truncate
        // it into a fake page. Preserve legacy artists/album/duration via existing songFrom.
        if (!list(object.value(QStringLiteral("songs")), kind == 5 ? 50 : 100, songs.songs,
                  songFrom))
            return invalid;
        songs.requestedPaginationUnverified = kind == 6;
        result.content = std::move(songs);
    } else if (kind == 8) {
        DiscoveryPlaylists playlists;
        if (!list(object.value(QStringLiteral("playlists")), maxRows, playlists.playlists,
                  discoveryPlaylistFrom))
            return invalid;
        result.content = std::move(playlists);
    } else
        return invalid;
    // If a candidate response explicitly echoes its seed, never accept another identity.
    QString key, seed;
    if (const auto* q = std::get_if<ArtistTopSongsQuery>(&query.action)) {
        key = QStringLiteral("artistId");
        seed = q->artist.value;
    } else if (const auto* q = std::get_if<SimilarArtistsQuery>(&query.action)) {
        key = QStringLiteral("artistId");
        seed = q->artist.value;
    } else if (const auto* q = std::get_if<SimilarSongsQuery>(&query.action)) {
        key = QStringLiteral("songId");
        seed = q->song.value;
    } else if (const auto* q = std::get_if<SimilarPlaylistsQuery>(&query.action)) {
        key = QStringLiteral("songId");
        seed = q->song.value;
    }
    if (!key.isEmpty() && !absent(object.value(key)) && providerId(object.value(key)) != seed)
        return invalid;
    return result;
}
} // namespace cirrus::netease
