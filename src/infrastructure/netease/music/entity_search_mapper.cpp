#include "entity_search_mapper.h"
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
bool pagination(const QJsonObject& o, const QString& count, std::optional<qint64>& total,
                std::optional<bool>& more)
{
    if (!number(o.value(count), total))
        return false;
    const auto flag = o.value(QStringLiteral("hasMore"));
    if (!absent(flag)) {
        if (!flag.isBool())
            return false;
        more = flag.toBool();
    }
    return true;
}
std::optional<QJsonArray> rows(const QJsonValue& v, int limit, std::optional<qint64> total,
                               std::optional<bool> more, bool knownZeroNull = false)
{
    // Observed voice API empty result: resources:null, totalCount:0, hasMore:false.
    if (knownZeroNull && absent(v) && total == 0 && (!more || !*more))
        return QJsonArray{};
    if (!v.isArray() || v.toArray().size() > limit || (total && *total < v.toArray().size()))
        return std::nullopt;
    return v.toArray();
}
std::optional<QJsonObject> resource(const QJsonValue& v, QStringView type)
{
    if (!v.isObject())
        return std::nullopt;
    const auto o = v.toObject();
    const auto base = o.value(QStringLiteral("baseInfo"));
    if (!base.isObject())
        return std::nullopt;
    const auto p = base.toObject();
    const auto id = providerId(p.value(QStringLiteral("id")));
    const auto raw = o.value(QStringLiteral("resourceId")),
               kind = o.value(QStringLiteral("resourceType"));
    if (id.isEmpty() || (!absent(raw) && providerId(raw) != id) ||
        (!absent(kind) && kind != QJsonValue(type.toString())))
        return std::nullopt;
    return p;
}
QJsonObject parameters(const SearchPage& p)
{
    return {{QStringLiteral("keywords"), p.keywords.trimmed()},
            {QStringLiteral("limit"), p.limit},
            {QStringLiteral("offset"), p.offset}};
}
} // namespace
std::optional<EntitySearchRequest> entitySearchRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<EntitySearchQuery>(&query)) {
        const auto op = q->kind == MusicSearchKind::Album    ? ApiOperation::SearchAlbums
                        : q->kind == MusicSearchKind::Artist ? ApiOperation::SearchArtists
                                                             : ApiOperation::SearchPlaylists;
        return EntitySearchRequest{op, parameters(q->page)};
    }
    if (const auto* q = std::get_if<PodcastSearchQuery>(&query))
        return EntitySearchRequest{ApiOperation::SearchPodcasts, parameters(q->page)};
    if (const auto* q = std::get_if<VoiceSearchQuery>(&query))
        return EntitySearchRequest{ApiOperation::SearchVoices, parameters(q->page)};
    return std::nullopt;
}
MusicResult mapEntitySearchResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!isEntitySearchQuery(query) || !validMusicQuery(query) ||
        object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (const auto* q = std::get_if<EntitySearchQuery>(&query)) {
        const auto value = object.value(QStringLiteral("result"));
        if (!value.isObject())
            return invalid;
        const auto o = value.toObject();
        const QString field = q->kind == MusicSearchKind::Album    ? QStringLiteral("albums")
                              : q->kind == MusicSearchKind::Artist ? QStringLiteral("artists")
                                                                   : QStringLiteral("playlists");
        const QString count = q->kind == MusicSearchKind::Album ? QStringLiteral("albumCount")
                              : q->kind == MusicSearchKind::Artist
                                  ? QStringLiteral("artistCount")
                                  : QStringLiteral("playlistCount");
        std::optional<qint64> total;
        std::optional<bool> more;
        if (!pagination(o, count, total, more))
            return invalid;
        const auto values = rows(o.value(field), q->page.limit, total, more);
        if (!values)
            return invalid;
        AlbumSearchPage albums{*q, {}, total, more};
        ArtistSearchPage artists{*q, {}, total, more};
        PlaylistSearchPage playlists{*q, {}, total, more};
        for (const auto v : *values) {
            if (!v.isObject())
                return invalid;
            const auto row = v.toObject();
            if (q->kind == MusicSearchKind::Album) {
                const auto a = albumFrom(row);
                if (!a)
                    return invalid;
                albums.albums.append(*a);
            } else if (q->kind == MusicSearchKind::Artist) {
                SearchArtist a;
                a.id = {providerId(row.value(QStringLiteral("id")))};
                const auto name = row.value(QStringLiteral("name"));
                if (a.id.value.isEmpty() || !textField(name) ||
                    !number(row.value(QStringLiteral("albumSize")), a.albumCount) ||
                    !number(row.value(QStringLiteral("musicSize")), a.songCount))
                    return invalid;
                a.name = name.toString();
                a.portrait = imageFrom(row.value(QStringLiteral("picUrl")));
                artists.artists.append(a);
            } else {
                const auto p = discoveryPlaylistFrom(row);
                if (!p)
                    return invalid;
                playlists.playlists.append(*p);
            }
        }
        if (q->kind == MusicSearchKind::Album)
            return albums;
        if (q->kind == MusicSearchKind::Artist)
            return artists;
        return playlists;
    }
    const bool pod = std::holds_alternative<PodcastSearchQuery>(query);
    const auto page =
        pod ? std::get<PodcastSearchQuery>(query).page : std::get<VoiceSearchQuery>(query).page;
    const auto value = object.value(QStringLiteral("data"));
    if (!value.isObject())
        return invalid;
    const auto o = value.toObject();
    std::optional<qint64> total;
    std::optional<bool> more;
    if (!pagination(o, QStringLiteral("totalCount"), total, more))
        return invalid;
    const auto values = rows(o.value(QStringLiteral("resources")), page.limit, total, more, true);
    if (!values)
        return invalid;
    PodcastSearchPage podcasts;
    VoiceSearchPage voices;
    if (pod)
        podcasts.query = std::get<PodcastSearchQuery>(query);
    else
        voices.query = std::get<VoiceSearchQuery>(query);
    podcasts.total = voices.total = total;
    podcasts.hasMore = voices.hasMore = more;
    for (const auto v : *values) {
        const auto base = resource(v, pod ? u"voicelist" : u"voice");
        if (!base)
            return invalid;
        if (pod) {
            const auto p = podcastFrom(*base);
            if (!p)
                return invalid;
            podcasts.podcasts.append(*p);
        } else {
            const auto p = voiceFrom(*base);
            if (!p)
                return invalid;
            voices.voices.append(*p);
        }
    }
    if (pod)
        return podcasts;
    return voices;
}
} // namespace cirrus::netease
