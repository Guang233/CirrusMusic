#include "recent_entity_mapper.h"
#include "music_mapping.h"
#include "spoken_mapping.h"
#include <QJsonArray>
namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }
bool number(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
}
// These recent resource envelopes are synthetic-only candidate schemas.
// No live account response was fetched; unsupported shapes fail explicitly.
template <class T, class Mapper>
MusicResult recent(const RecentEntitiesQuery& q, const QJsonObject& data, QStringView type,
                   Mapper mapper)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    CloudRecentEntities<T> page;
    page.query = q;
    const auto values = data.value(QStringLiteral("list"));
    const auto uid = data.value(QStringLiteral("uid"));
    if ((!absent(uid) && providerId(uid) != q.accountId) || !values.isArray() ||
        values.toArray().size() > q.limit ||
        !number(data.value(QStringLiteral("total")), page.total) ||
        (page.total && *page.total < values.toArray().size()))
        return invalid;
    for (const auto v : values.toArray()) {
        if (!v.isObject())
            return invalid;
        const auto row = v.toObject();
        const auto raw = row.value(QStringLiteral("data"));
        if (!raw.isObject())
            return invalid;
        const auto entity = mapper(raw.toObject());
        if (!entity)
            return invalid;
        const auto id = row.value(QStringLiteral("resourceId")),
                   kind = row.value(QStringLiteral("resourceType"));
        if ((!absent(id) && providerId(id) != entity->id.value) ||
            (!absent(kind) && kind != QJsonValue(type.toString())))
            return invalid;
        RecentEntity<T> item;
        item.entity = *entity;
        if (!number(row.value(QStringLiteral("playTime")), item.providerPlayTime))
            return invalid;
        page.items.append(item);
    }
    return page;
}
} // namespace
std::optional<RecentEntityRequest> recentEntityRequest(const MusicQuery& query)
{
    const auto* q = std::get_if<RecentEntitiesQuery>(&query);
    if (!q || !q->isValid())
        return std::nullopt;
    ApiOperation op;
    switch (q->kind) {
    case RecentEntityKind::Playlist:
        op = ApiOperation::RecentPlaylists;
        break;
    case RecentEntityKind::Album:
        op = ApiOperation::RecentAlbums;
        break;
    case RecentEntityKind::Podcast:
        op = ApiOperation::RecentPodcasts;
        break;
    case RecentEntityKind::Voice:
        op = ApiOperation::RecentVoices;
        break;
    default:
        return std::nullopt;
    }
    return RecentEntityRequest{op, {{QStringLiteral("limit"), q->limit}}};
}
MusicResult mapRecentEntityResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    const auto* q = std::get_if<RecentEntitiesQuery>(&query);
    if (!q || !q->isValid() || object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    const auto uid = object.value(QStringLiteral("uid"));
    if (!absent(uid) && mapping::providerId(uid) != q->accountId)
        return invalid;
    const auto raw = object.value(QStringLiteral("data"));
    if (!raw.isObject())
        return invalid;
    const auto data = raw.toObject();
    switch (q->kind) {
    case RecentEntityKind::Playlist:
        return recent<DiscoveryPlaylist>(*q, data, u"PLAYLIST", mapping::discoveryPlaylistFrom);
    case RecentEntityKind::Album:
        return recent<Album>(*q, data, u"ALBUM", mapping::albumFrom);
    case RecentEntityKind::Podcast:
        return recent<Podcast>(*q, data, u"DJRADIO", mapping::podcastFrom);
    case RecentEntityKind::Voice:
        return recent<Voice>(*q, data, u"VOICE", mapping::voiceFrom);
    }
    return invalid;
}
} // namespace cirrus::netease
