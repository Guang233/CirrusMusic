// Request structures from fixed api-enhanced dj_sublist/user_cloud* (MIT); see NOTICE.
#include "account_library_mapper.h"
#include "music_mapping.h"
#include "spoken_mapping.h"
#include <QHash>
#include <QJsonArray>
#include <QSet>
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
bool quota(const QJsonValue& v, std::optional<qint64>& out)
{
    if (!v.isString())
        return number(v, out);
    const auto text = v.toString();
    if (text != QStringLiteral("0") && !validMusicId(text))
        return false;
    out = text.toLongLong(); // canonical nonnegative signed64 text, validated before conversion
    return true;
}
bool boolean(const QJsonValue& v, std::optional<bool>& out)
{
    if (absent(v))
        return true;
    if (!v.isBool())
        return false;
    out = v.toBool();
    return true;
}
bool text(const QJsonValue& v, std::optional<QString>& out)
{
    if (!textField(v))
        return false;
    if (!absent(v))
        out = v.toString();
    return true;
}
std::optional<CloudSong> cloudSong(const QJsonObject& o)
{
    CloudSong song;
    song.id = {providerId(o.value(QStringLiteral("songId")))};
    const auto title = o.value(QStringLiteral("songName"));
    if (song.id.value.isEmpty() || !textField(title) ||
        !text(o.value(QStringLiteral("artist")), song.artist) ||
        !text(o.value(QStringLiteral("album")), song.album) ||
        !text(o.value(QStringLiteral("fileName")), song.fileName) ||
        !number(o.value(QStringLiteral("fileSize")), song.providerFileSize) ||
        !number(o.value(QStringLiteral("bitrate")), song.providerBitrate) ||
        !number(o.value(QStringLiteral("addTime")), song.providerAddedTime))
        return std::nullopt;
    song.title = title.toString();
    const auto nested = o.value(QStringLiteral("simpleSong"));
    if (!absent(nested)) {
        if (!nested.isObject())
            return std::nullopt;
        const auto raw = nested.toObject().value(QStringLiteral("id"));
        if (!unknownId(raw)) {
            const auto id = providerId(raw);
            if (id.isEmpty())
                return std::nullopt;
            song.catalogReference = SongId{id};
        }
        // Lenient: a row whose simpleSong does not read as a song still lists.
        song.track = songFrom(nested.toObject());
    }
    // The app's list says so (matchType); otherwise a file-only song has no catalog album.
    const auto matchType = o.value(QStringLiteral("matchType"));
    if (matchType.isString())
        song.matched = matchType.toString() == QStringLiteral("matched");
    else if (song.track)
        song.matched = song.track->album && song.track->album->id.has_value();
    // Ignore private file identifiers, URLs, profiles and rights. No inferred ID
    // binding/entitlement.
    return song;
}
QJsonObject pageParameters(CatalogPage page)
{
    return {{QStringLiteral("limit"), page.limit}, {QStringLiteral("offset"), page.offset}};
}
} // namespace
std::optional<AccountLibraryRequest> accountLibraryRequest(const MusicQuery& query)
{
    if (!isAccountLibraryQuery(query) || !validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<SubscribedPodcastsQuery>(&query))
        return AccountLibraryRequest{ApiOperation::SubscribedPodcasts, pageParameters(q->page)};
    if (const auto* q = std::get_if<CloudSongsQuery>(&query))
        return AccountLibraryRequest{ApiOperation::CloudSongs, pageParameters(q->page)};
    const auto& q = std::get<CloudSongDetailsQuery>(query);
    QJsonArray ids;
    for (const auto& song : q.songs)
        ids.append(song.value);
    return AccountLibraryRequest{ApiOperation::CloudSongDetails, {{QStringLiteral("ids"), ids}}};
}
MusicResult mapAccountLibraryResponse(const MusicQuery& query, const QJsonObject& o)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!isAccountLibraryQuery(query) || !validMusicQuery(query) ||
        o.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    const auto account = musicQueryAccount(query);
    const auto uid = o.value(QStringLiteral("uid"));
    if (!account || (!absent(uid) && providerId(uid) != *account))
        return invalid;
    if (const auto* q = std::get_if<SubscribedPodcastsQuery>(&query)) {
        const auto rows = o.value(QStringLiteral("djRadios"));
        SubscribedPodcastsPage page;
        page.query = *q;
        if (!rows.isArray() || rows.toArray().size() > q->page.limit ||
            !number(o.value(QStringLiteral("count")), page.total) ||
            (page.total && *page.total < rows.toArray().size()) ||
            !number(o.value(QStringLiteral("time")), page.providerTime) ||
            !boolean(o.value(QStringLiteral("hasMore")), page.hasMore))
            return invalid;
        for (const auto v : rows.toArray()) {
            if (!v.isObject())
                return invalid;
            const auto podcast = podcastFrom(v.toObject());
            if (!podcast)
                return invalid;
            page.podcasts.append(*podcast);
        }
        return page;
    }
    const auto rows = o.value(QStringLiteral("data"));
    if (!rows.isArray())
        return invalid;
    if (const auto* q = std::get_if<CloudSongsQuery>(&query)) {
        CloudSongsPage page;
        page.query = *q;
        if (rows.toArray().size() > q->page.limit ||
            !number(o.value(QStringLiteral("count")), page.total) ||
            (page.total && *page.total < rows.toArray().size()) ||
            !boolean(o.value(QStringLiteral("hasMore")), page.hasMore) ||
            !quota(o.value(QStringLiteral("size")), page.providerUsedSpace) ||
            !quota(o.value(QStringLiteral("maxSize")), page.providerMaximumSpace))
            return invalid;
        for (const auto v : rows.toArray()) {
            if (!v.isObject())
                return invalid;
            const auto song = cloudSong(v.toObject());
            if (!song)
                return invalid;
            page.songs.append(*song);
        }
        return page;
    }
    const auto& q = std::get<CloudSongDetailsQuery>(query);
    if (rows.toArray().size() > q.songs.size())
        return invalid;
    QSet<QString> requested;
    for (const auto& id : q.songs)
        requested.insert(id.value);
    QHash<QString, CloudSong> mapped;
    for (const auto v : rows.toArray()) {
        if (!v.isObject())
            return invalid;
        const auto song = cloudSong(v.toObject());
        if (!song || !requested.contains(song->id.value) || mapped.contains(song->id.value))
            return invalid;
        mapped.insert(song->id.value, *song);
    }
    CloudSongDetails page;
    page.query = q;
    for (const auto& id : q.songs) {
        const auto it = mapped.constFind(id.value);
        if (it == mapped.cend())
            page.missing.append(id);
        else
            page.songs.append(*it);
    }
    return page;
}
} // namespace cirrus::netease
