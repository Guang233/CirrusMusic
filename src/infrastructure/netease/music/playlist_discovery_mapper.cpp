#include "playlist_discovery_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <cmath>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isNull() || v.isUndefined(); }
bool number(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
}
bool flag(const QJsonValue& v, std::optional<bool>& out)
{
    if (absent(v))
        return true;
    if (!v.isBool())
        return false;
    out = v.toBool();
    return true;
}
bool pageInfo(const QJsonObject& o, qsizetype size, std::optional<qint64>& total,
              std::optional<bool>& more)
{
    return number(o.value(QStringLiteral("total")), total) &&
           flag(o.value(QStringLiteral("more")), more) && (!total || *total >= size);
}
std::optional<QList<DiscoveryPlaylist>> playlists(const QJsonValue& values, int limit,
                                                  bool recommended, bool daily = false)
{
    if (!values.isArray() || values.toArray().size() > limit)
        return std::nullopt;
    QList<DiscoveryPlaylist> result;
    for (const auto value : values.toArray()) {
        if (!value.isObject())
            return std::nullopt;
        const auto row = value.toObject();
        DiscoveryPlaylist item;
        item.id.value = providerId(row.value(QStringLiteral("id")));
        const auto name = row.value(QStringLiteral("name"));
        const auto summary =
            row.value(recommended ? QStringLiteral("copywriter") : QStringLiteral("description"));
        if (item.id.value.isEmpty() || !textField(name) || (daily && !name.isString()) ||
            !textField(summary) ||
            !number(row.value(QStringLiteral("trackCount")), item.trackCount) ||
            !number(row.value(QStringLiteral("updateTime")), item.providerUpdateTime))
            return std::nullopt;
        item.title = name.toString();
        if (!absent(summary))
            item.summary = summary.toString();
        item.cover = imageFrom(
            row.value(recommended ? QStringLiteral("picUrl") : QStringLiteral("coverImgUrl")));
        const auto plays =
            row.value(daily ? QStringLiteral("playcount") : QStringLiteral("playCount"));
        if (!absent(plays)) {
            if (!plays.isDouble() || !std::isfinite(plays.toDouble()) || plays.toDouble() < 0 ||
                plays.toDouble() > double(MaximumPlaylistCursor))
                return std::nullopt;
            item.playCount = plays.toDouble();
        }
        result.append(item); // no deduplication or inferred ownership/liked state
    }
    return result;
}
std::optional<PlaylistTag> tag(const QJsonValue& value)
{
    if (!value.isObject())
        return std::nullopt;
    const auto o = value.toObject();
    const auto name = o.value(QStringLiteral("name"));
    PlaylistTag result;
    if (!name.isString() || !validPlaylistCategory(name.toString()) ||
        !number(o.value(QStringLiteral("category")), result.categoryCode) ||
        !flag(o.value(QStringLiteral("hot")), result.hot))
        return std::nullopt;
    result.name = name.toString();
    return result;
}
} // namespace
std::optional<PlaylistDiscoveryRequest> playlistDiscoveryRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (std::holds_alternative<DailyRecommendedPlaylistsQuery>(query))
        return PlaylistDiscoveryRequest{ApiOperation::DailyRecommendedPlaylists, {}};
    if (const auto* q = std::get_if<RecommendedPlaylistsQuery>(&query))
        return PlaylistDiscoveryRequest{ApiOperation::RecommendedPlaylists,
                                        {{QStringLiteral("limit"), q->limit}}};
    if (const auto* q = std::get_if<PlaylistListQuery>(&query))
        return PlaylistDiscoveryRequest{
            ApiOperation::PlaylistList,
            {{QStringLiteral("cat"), q->category},
             {QStringLiteral("order"),
              q->order == PlaylistOrder::Hot ? QStringLiteral("hot") : QStringLiteral("new")},
             {QStringLiteral("limit"), q->page.limit},
             {QStringLiteral("offset"), q->page.offset}}};
    if (const auto* q = std::get_if<PremiumPlaylistsQuery>(&query))
        return PlaylistDiscoveryRequest{ApiOperation::PremiumPlaylists,
                                        {{QStringLiteral("cat"), q->category},
                                         {QStringLiteral("limit"), q->limit},
                                         {QStringLiteral("before"), q->before}}};
    if (const auto* q = std::get_if<PlaylistTagsQuery>(&query)) {
        const auto op = q->kind == PlaylistTagsKind::Catalogue ? ApiOperation::PlaylistCatalogue
                        : q->kind == PlaylistTagsKind::Hot     ? ApiOperation::HotPlaylistTags
                                                               : ApiOperation::PremiumPlaylistTags;
        return PlaylistDiscoveryRequest{op, {}};
    }
    return std::nullopt;
}
MusicResult mapPlaylistDiscoveryResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!isPlaylistDiscoveryQuery(query) || !validMusicQuery(query) ||
        object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (const auto* q = std::get_if<DailyRecommendedPlaylistsQuery>(&query)) {
        const auto rows = playlists(object.value(QStringLiteral("recommend")), 100, true, true);
        if (!rows)
            return invalid;
        return DailyRecommendedPlaylists{*q, *rows};
    }
    if (const auto* q = std::get_if<RecommendedPlaylistsQuery>(&query)) {
        const auto rows = playlists(object.value(QStringLiteral("result")), q->limit, true);
        if (!rows)
            return invalid;
        return RecommendedPlaylists{*q, *rows};
    }
    if (const auto* q = std::get_if<PlaylistListQuery>(&query)) {
        const auto rows =
            playlists(object.value(QStringLiteral("playlists")), q->page.limit, false);
        PlaylistListPage page;
        page.query = *q;
        if (!rows || !pageInfo(object, rows->size(), page.total, page.hasMore))
            return invalid;
        page.playlists = *rows;
        return page;
    }
    if (const auto* q = std::get_if<PremiumPlaylistsQuery>(&query)) {
        const auto rows = playlists(object.value(QStringLiteral("playlists")), q->limit, false);
        PremiumPlaylistsPage page;
        page.query = *q;
        if (!rows || !pageInfo(object, rows->size(), page.total, page.hasMore) ||
            !number(object.value(QStringLiteral("lasttime")), page.providerLastTime))
            return invalid;
        page.playlists = *rows;
        return page;
    }
    const auto q = std::get<PlaylistTagsQuery>(query);
    const bool catalogue = q.kind == PlaylistTagsKind::Catalogue;
    const auto values = object.value(catalogue ? QStringLiteral("sub") : QStringLiteral("tags"));
    const auto limit =
        q.kind == PlaylistTagsKind::Hot ? PlaylistTags::MaximumHotTags : PlaylistTags::MaximumTags;
    if (!values.isArray() || values.toArray().size() > limit)
        return invalid;
    PlaylistTags result;
    result.query = q;
    for (const auto value : values.toArray()) {
        const auto item = tag(value);
        if (!item)
            return invalid;
        result.tags.append(*item);
    }
    if (catalogue) {
        const auto all = object.value(QStringLiteral("all"));
        if (!absent(all)) {
            result.all = tag(all);
            if (!result.all)
                return invalid;
        }
        const auto groups = object.value(QStringLiteral("categories"));
        if (!absent(groups)) {
            if (!groups.isObject() || groups.toObject().size() > PlaylistTags::MaximumGroups)
                return invalid;
            result.categories = QMap<qint64, QString>{};
            const auto dictionary = groups.toObject();
            for (auto it = dictionary.begin(); it != dictionary.end(); ++it) {
                bool ok = false;
                const auto code = it.key().toLongLong(&ok);
                if (!ok || code < 0 || code > MaximumPlaylistCursor ||
                    QString::number(code) != it.key() || !it.value().isString() ||
                    !validPlaylistCategory(it.value().toString()))
                    return invalid;
                result.categories->insert(code, it.value().toString());
            }
        }
    }
    // Hot/premium tags do not manufacture the general catalogue's groups/all entry.
    return result;
}
} // namespace cirrus::netease
