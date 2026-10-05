#pragma once
#include "catalog_types.h"
#include <QMap>

namespace cirrus::domain
{
inline constexpr qint64 MaximumPlaylistCursor = 9007199254740991LL;
inline bool validPlaylistCategory(const QString& value)
{
    if (value.isEmpty() || value.size() > 128 || !value.isValidUtf16() || value.trimmed() != value)
        return false;
    for (const auto c : value)
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return false;
    return true;
}
struct RecommendedPlaylistsQuery {
    int limit = 30;
    [[nodiscard]] bool isValid() const { return limit >= 1 && limit <= 100; }
};
enum class PlaylistOrder { Hot, New };
struct PlaylistListQuery {
    QString category = QStringLiteral("全部");
    CatalogPage page{50, 0};
    PlaylistOrder order = PlaylistOrder::Hot;
    [[nodiscard]] bool isValid() const
    {
        return validPlaylistCategory(category) && page.isValid() &&
               (order == PlaylistOrder::Hot || order == PlaylistOrder::New);
    }
};
struct PremiumPlaylistsQuery {
    QString category = QStringLiteral("全部");
    int limit = 50;
    qint64 before = 0; // 0 = first page; previous page's last playlist updateTime, NOT offset
    [[nodiscard]] bool isValid() const
    {
        return validPlaylistCategory(category) && limit >= 1 && limit <= 100 && before >= 0 &&
               before <= MaximumPlaylistCursor;
    }
};
enum class PlaylistTagsKind { Catalogue, Hot, Premium };
struct PlaylistTagsQuery {
    PlaylistTagsKind kind = PlaylistTagsKind::Catalogue;
    [[nodiscard]] bool isValid() const
    {
        return kind == PlaylistTagsKind::Catalogue || kind == PlaylistTagsKind::Hot ||
               kind == PlaylistTagsKind::Premium;
    }
};
struct DiscoveryPlaylist {
    PlaylistId id;
    QString title;
    QUrl cover;
    std::optional<QString> summary;
    std::optional<qint64> trackCount, providerUpdateTime;
    std::optional<double> playCount; // provider aggregate, NOT this account's count or duration
};
struct RecommendedPlaylists {
    RecommendedPlaylistsQuery query;
    QList<DiscoveryPlaylist> playlists; // no offset/hasMore/implicit details
};
struct PlaylistListPage {
    PlaylistListQuery query;
    QList<DiscoveryPlaylist> playlists;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct PremiumPlaylistsPage {
    PremiumPlaylistsQuery query;
    QList<DiscoveryPlaylist> playlists;
    std::optional<qint64> total, providerLastTime; // optional raw root.lasttime, no guessed cursor
    std::optional<bool> hasMore;
};
struct PlaylistTag {
    QString name; // opaque category label, NOT a music/resource ID
    std::optional<qint64> categoryCode;
    std::optional<bool> hot;
};
struct PlaylistTags {
    static constexpr qsizetype MaximumTags = 500, MaximumHotTags = 100, MaximumGroups = 50;
    PlaylistTagsQuery query;
    QList<PlaylistTag> tags; // provider array order and duplicates retained
    std::optional<PlaylistTag> all;
    std::optional<QMap<qint64, QString>> categories; // dictionary; no provider order inferred
};
} // namespace cirrus::domain
