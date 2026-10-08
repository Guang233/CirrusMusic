#include "discovery_browse_operation.h"
#include <type_traits>

namespace cirrus::netease
{
namespace
{
using namespace domain;
const QStringList Areas{"ALL", "ZH", "EA", "KR", "JP"};
QString areaName(AlbumArea a) { return Areas.at(int(a)); }
QJsonObject pageParameters(CatalogPage p)
{
    return {{QStringLiteral("limit"), p.limit}, {QStringLiteral("offset"), p.offset}};
}
bool pageValid(const QJsonObject& p)
{
    const auto l = p.value(QStringLiteral("limit")), o = p.value(QStringLiteral("offset"));
    const CatalogPage page{l.toInt(-1), o.toInt(-1)};
    return page.isValid() && l == QJsonValue(page.limit) && o == QJsonValue(page.offset);
}
bool canonicalNumber(const QJsonValue& v, int& out)
{
    bool ok = false;
    out = v.toString().toInt(&ok);
    return v.isString() && ok && v.toString() == QString::number(out);
}
} // namespace
bool isDiscoveryBrowseOperation(ApiOperation op)
{
    return op >= ApiOperation::LatestAlbums && op <= ApiOperation::SimilarPlaylists;
}
std::optional<ApiOperationContract> discoveryBrowseContract(ApiOperation op)
{
    static const QStringList paths{"/api/discovery/newAlbum",
                                   "/api/album/new",
                                   "/api/discovery/new/albums/area",
                                   "/api/v1/artist/list",
                                   "/api/artist/top",
                                   "/api/artist/top/song",
                                   "/api/v1/discovery/simiSong",
                                   "/api/discovery/simiArtist",
                                   "/api/discovery/simiPlaylist"};
    if (!isDiscoveryBrowseOperation(op))
        return std::nullopt;
    return ApiOperationContract{paths.at(int(op) - int(ApiOperation::LatestAlbums)),
                                Protocol::Weapi, ResponseEncoding::Json, 2 * 1024 * 1024};
}
std::optional<DiscoveryBrowseWire> discoveryBrowseRequest(const DiscoveryBrowseQuery& query)
{
    if (!query.isValid())
        return std::nullopt;
    DiscoveryBrowseWire out{
        ApiOperation(int(ApiOperation::LatestAlbums) + int(query.action.index())), {}};
    std::visit(
        [&](const auto& q) {
            using T = std::decay_t<decltype(q)>;
            if constexpr (requires { q.page; })
                out.parameters = pageParameters(q.page);
            if constexpr (std::is_same_v<T, NewAlbumsQuery> || std::is_same_v<T, AlbumChartsQuery>)
                out.parameters.insert(QStringLiteral("area"), areaName(q.area));
            if constexpr (std::is_same_v<T, AlbumChartsQuery>) {
                out.parameters.insert(QStringLiteral("year"), q.year);
                out.parameters.insert(QStringLiteral("month"), q.month);
                out.parameters.insert(QStringLiteral("type"),
                                      q.order == AlbumChartOrder::New ? "new" : "hot");
            }
            if constexpr (std::is_same_v<T, ArtistBrowseQuery>) {
                out.parameters.insert(QStringLiteral("type"), QString::number(int(q.category)));
                out.parameters.insert(QStringLiteral("area"), QString::number(int(q.area)));
                out.parameters.insert(QStringLiteral("initial"), q.initial);
            }
            if constexpr (std::is_same_v<T, ArtistTopSongsQuery>)
                out.parameters.insert(QStringLiteral("id"), q.artist.value);
            if constexpr (std::is_same_v<T, SimilarArtistsQuery>)
                out.parameters.insert(QStringLiteral("artistid"), q.artist.value);
            if constexpr (std::is_same_v<T, SimilarSongsQuery> ||
                          std::is_same_v<T, SimilarPlaylistsQuery>)
                out.parameters.insert(QStringLiteral("songid"), q.song.value);
        },
        query.action);
    return out;
}
std::optional<QJsonObject> discoveryBrowsePayload(ApiOperation op, const QJsonObject& p)
{
    if (!isDiscoveryBrowseOperation(op))
        return std::nullopt;
    if (op == ApiOperation::LatestAlbums)
        return p.isEmpty() ? std::optional<QJsonObject>(p) : std::nullopt;
    if (op == ApiOperation::ArtistTopSongs || op == ApiOperation::SimilarArtists) {
        const auto key =
            op == ApiOperation::ArtistTopSongs ? QStringLiteral("id") : QStringLiteral("artistid");
        const auto v = p.value(key);
        if (p.size() != 1 || !v.isString() || !domain::validMusicId(v.toString()))
            return std::nullopt;
        return p;
    }
    if (!pageValid(p))
        return std::nullopt;
    QJsonObject payload = p;
    if (op == ApiOperation::SimilarSongs || op == ApiOperation::SimilarPlaylists) {
        const auto id = p.value(QStringLiteral("songid"));
        if (p.size() != 3 || !id.isString() || !domain::validMusicId(id.toString()))
            return std::nullopt;
        return payload;
    }
    if (op == ApiOperation::PopularArtists) {
        if (p.size() != 2)
            return std::nullopt;
        payload.insert(QStringLiteral("total"), true);
        return payload;
    }
    if (op == ApiOperation::ArtistBrowse) {
        int category = 0, area = 0;
        const auto raw = p.value(QStringLiteral("initial"));
        const ArtistBrowseQuery q{ArtistCategory(category), ArtistArea(area), raw.toInt(-2)};
        if (p.size() != 5 || !canonicalNumber(p.value(QStringLiteral("type")), category) ||
            !canonicalNumber(p.value(QStringLiteral("area")), area) ||
            !ArtistBrowseQuery{ArtistCategory(category), ArtistArea(area), q.initial}.isValid() ||
            raw != QJsonValue(q.initial))
            return std::nullopt;
        payload.insert(QStringLiteral("total"), true);
        return payload;
    }
    const auto area = p.value(QStringLiteral("area"));
    if (!area.isString() || !Areas.contains(area.toString()))
        return std::nullopt;
    if (op == ApiOperation::NewAlbums) {
        if (p.size() != 3)
            return std::nullopt;
        payload.insert(QStringLiteral("total"), true);
        return payload;
    }
    const auto year = p.value(QStringLiteral("year")), month = p.value(QStringLiteral("month"));
    const auto type = p.value(QStringLiteral("type"));
    const AlbumChartsQuery q{year.toInt(-1), month.toInt(-1)};
    if (op != ApiOperation::AlbumCharts || p.size() != 6 || !q.isValid() ||
        year != QJsonValue(q.year) || month != QJsonValue(q.month) ||
        (type != QJsonValue("new") && type != QJsonValue("hot")))
        return std::nullopt;
    payload.insert(QStringLiteral("total"), false);
    payload.insert(QStringLiteral("rcmd"), true);
    return payload;
}
} // namespace cirrus::netease
