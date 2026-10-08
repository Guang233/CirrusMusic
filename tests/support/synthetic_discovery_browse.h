#pragma once
#include "domain/music/discovery_browse_types.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace cirrus::test
{
inline QList<domain::DiscoveryBrowseQuery> discoveryBrowseQueries()
{
    using namespace domain;
    return {{LatestAlbumsQuery{}},
            {NewAlbumsQuery{AlbumArea::Western, {2, 10}}},
            {AlbumChartsQuery{2026, 10, AlbumArea::Japanese, {2, 10}, AlbumChartOrder::Hot}},
            {ArtistBrowseQuery{ArtistCategory::Band, ArtistArea::Other, 65, {2, 10}}},
            {PopularArtistsQuery{{2, 10}}},
            {ArtistTopSongsQuery{{"9007199254740993"}}},
            {SimilarSongsQuery{{"9007199254740995"}, {2, 10}}},
            {SimilarArtistsQuery{{"9007199254740993"}}},
            {SimilarPlaylistsQuery{{"9007199254740995"}, {2, 10}}}};
}
inline QJsonObject discoveryBrowseResponse(int kind)
{
    const QJsonObject item{{QStringLiteral("id"), QStringLiteral("9007199254740997")},
                           {QStringLiteral("name"), QStringLiteral(" synthetic 雲 & <text> ")}};
    QJsonObject o{{QStringLiteral("code"), 200}};
    if (kind == 2) {
        o.insert(QStringLiteral("weekData"), QJsonArray{item});
        o.insert(QStringLiteral("monthData"), QJsonArray{item, item});
    } else {
        const auto field = kind <= 1                               ? QStringLiteral("albums")
                           : (kind == 3 || kind == 4 || kind == 7) ? QStringLiteral("artists")
                           : kind == 8                             ? QStringLiteral("playlists")
                                                                   : QStringLiteral("songs");
        o.insert(field, QJsonArray{item, item});
    }
    return o; // synthetic in-memory metadata only, no provider responses or images
}
inline QByteArray discoveryBrowseBody(int kind)
{
    return QJsonDocument(discoveryBrowseResponse(kind)).toJson(QJsonDocument::Compact);
}
} // namespace cirrus::test
