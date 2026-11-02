#include "library_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <QSet>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& value) { return value.isNull() || value.isUndefined(); }
// The provider's playlist kind for liked songs (candidate, from the reference's usage).
constexpr qint64 LibraryLikedSongsSpecialType = 5;
bool number(const QJsonValue& value, std::optional<qint64>& out)
{
    if (absent(value))
        return true;
    out = nonnegative(value);
    return out.has_value();
}
bool boolean(const QJsonValue& value, std::optional<bool>& out)
{
    if (absent(value))
        return true;
    if (!value.isBool())
        return false;
    out = value.toBool();
    return true;
}
bool pageInfo(const QJsonObject& object, bool playlists, qsizetype size,
              std::optional<qint64>& total, std::optional<bool>& more)
{
    return number(object.value(playlists ? QStringLiteral("total") : QStringLiteral("count")),
                  total) &&
           boolean(object.value(playlists ? QStringLiteral("more") : QStringLiteral("hasMore")),
                   more) &&
           (!total || *total >= size);
}
QJsonObject pageParameters(CatalogPage page)
{
    return {{QStringLiteral("limit"), page.limit}, {QStringLiteral("offset"), page.offset}};
}
} // namespace
std::optional<LibraryRequest> libraryRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<MyPlaylistsQuery>(&query)) {
        auto parameters = pageParameters(q->page);
        parameters.insert(QStringLiteral("uid"), q->accountId);
        return LibraryRequest{ApiOperation::MyPlaylists, parameters};
    }
    if (const auto* q = std::get_if<LikedSongIdsQuery>(&query))
        return LibraryRequest{ApiOperation::LikedSongIds, {{QStringLiteral("uid"), q->accountId}}};
    if (const auto* q = std::get_if<SavedAlbumsQuery>(&query))
        return LibraryRequest{ApiOperation::SavedAlbums, pageParameters(q->page)};
    if (const auto* q = std::get_if<SavedArtistsQuery>(&query))
        return LibraryRequest{ApiOperation::SavedArtists, pageParameters(q->page)};
    return std::nullopt;
}
MusicResult mapLibraryResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    const auto account = libraryQueryAccount(query);
    if (!account || !validMusicQuery(query) ||
        object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    const auto uid = object.value(QStringLiteral("uid"));
    if (!absent(uid) && providerId(uid) != *account)
        return invalid;
    if (std::holds_alternative<LikedSongIdsQuery>(query)) {
        const auto values = object.value(QStringLiteral("ids"));
        if (!values.isArray() || values.toArray().size() > LikedSongIds::MaximumIds)
            return invalid;
        LikedSongIds result{*account, {}};
        QSet<QString> seen;
        for (const auto value : values.toArray()) {
            const auto id = providerId(value);
            if (id.isEmpty() || seen.contains(id))
                return invalid;
            seen.insert(id);
            result.ids.append({id});
        }
        return result;
    }
    if (const auto* q = std::get_if<MyPlaylistsQuery>(&query)) {
        const auto values = object.value(QStringLiteral("playlist"));
        MyPlaylistsPage page;
        page.query = *q;
        if (!values.isArray() || values.toArray().size() > q->page.limit ||
            !pageInfo(object, true, values.toArray().size(), page.total, page.hasMore))
            return invalid;
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto row = value.toObject();
            LibraryPlaylist p;
            p.id = {providerId(row.value(QStringLiteral("id")))};
            if (p.id.value.isEmpty() || !textField(row.value(QStringLiteral("name"))) ||
                !number(row.value(QStringLiteral("trackCount")), p.trackCount) ||
                !boolean(row.value(QStringLiteral("subscribed")), p.subscribed))
                return invalid;
            const auto creator = row.value(QStringLiteral("creator"));
            if (!absent(creator)) {
                if (!creator.isObject())
                    return invalid;
                const auto raw = creator.toObject().value(QStringLiteral("userId"));
                if (!unknownId(raw)) {
                    const auto id = providerId(raw);
                    if (id.isEmpty())
                        return invalid;
                    p.creatorId = id;
                }
            }
            // Candidate: specialType 5 marks a liked-songs playlist; only the account's own
            // one is its library's "我喜欢的音乐". Any other kind is a plain playlist here.
            std::optional<qint64> specialType;
            if (!number(row.value(QStringLiteral("specialType")), specialType))
                return invalid;
            if (specialType && *specialType != LibraryLikedSongsSpecialType)
                p.accountLikedSongs = false;
            else if (specialType && p.creatorId)
                p.accountLikedSongs = *p.creatorId == *account;
            p.title = row.value(QStringLiteral("name")).toString();
            p.cover = imageFrom(row.value(QStringLiteral("coverImgUrl")));
            page.playlists.append(p);
        }
        return page;
    }
    const auto values = object.value(QStringLiteral("data"));
    if (!values.isArray())
        return invalid;
    if (const auto* q = std::get_if<SavedAlbumsQuery>(&query)) {
        SavedAlbumsPage page;
        page.query = *q;
        if (values.toArray().size() > q->page.limit ||
            !pageInfo(object, false, values.toArray().size(), page.total, page.hasMore))
            return invalid;
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto album = albumFrom(value.toObject());
            if (!album)
                return invalid;
            page.albums.append(*album);
        }
        return page;
    }
    if (const auto* q = std::get_if<SavedArtistsQuery>(&query)) {
        SavedArtistsPage page;
        page.query = *q;
        if (values.toArray().size() > q->page.limit ||
            !pageInfo(object, false, values.toArray().size(), page.total, page.hasMore))
            return invalid;
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto row = value.toObject();
            SavedArtist artist;
            artist.id = {providerId(row.value(QStringLiteral("id")))};
            if (artist.id.value.isEmpty() || !textField(row.value(QStringLiteral("name"))) ||
                !number(row.value(QStringLiteral("albumSize")), artist.albumCount))
                return invalid;
            artist.name = row.value(QStringLiteral("name")).toString();
            artist.portrait = imageFrom(row.value(QStringLiteral("picUrl")));
            page.artists.append(artist);
        }
        return page;
    }
    return invalid;
}
} // namespace cirrus::netease
