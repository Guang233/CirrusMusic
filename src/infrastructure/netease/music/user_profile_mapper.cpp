#include "user_profile_mapper.h"
#include "cloud_history_mapper.h"
#include "library_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <type_traits>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& value) { return value.isNull() || value.isUndefined(); }
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
MusicFailure invalidBecause(const char* reason, qsizetype row = -1)
{
    rejectedBecause(reason, row);
    return MusicFailure{MusicError::InvalidResponse};
}
// Nickname, avatar and signature shared by the profile and the list rows.
bool personFrom(const QJsonObject& o, QString& id, QString& nickname, QUrl& avatar,
                QString& signature, std::optional<bool>& followed)
{
    id = providerId(o.value(QStringLiteral("userId")));
    const auto name = o.value(QStringLiteral("nickname"));
    const auto sign = o.value(QStringLiteral("signature"));
    if (id.isEmpty() || !name.isString() || !textField(name) || !textField(sign) ||
        !boolean(o.value(QStringLiteral("followed")), followed))
        return false;
    nickname = name.toString();
    signature = sign.toString();
    avatar = imageFrom(o.value(QStringLiteral("avatarUrl")));
    return true;
}
MusicResult profileFrom(const UserProfileQuery& query, const QJsonObject& object)
{
    const auto raw = object.value(QStringLiteral("profile"));
    if (!raw.isObject())
        return invalidBecause("profile");
    const auto o = raw.toObject();
    UserProfile p;
    if (!personFrom(o, p.userId, p.nickname, p.avatar, p.signature, p.followed) ||
        p.userId != query.userId())
        return invalidBecause("user");
    if (!number(object.value(QStringLiteral("level")), p.level) ||
        !number(o.value(QStringLiteral("follows")), p.follows) ||
        !number(o.value(QStringLiteral("followeds")), p.fans) ||
        !number(o.value(QStringLiteral("playlistCount")), p.playlistCount) ||
        !boolean(object.value(QStringLiteral("peopleCanSeeMyPlayRecord")), p.rankingOpen))
        return invalidBecause("counts");
    const auto artist = o.value(QStringLiteral("artistId"));
    if (!unknownId(artist)) {
        const auto id = providerId(artist);
        if (id.isEmpty())
            return invalidBecause("artist");
        p.artist = ArtistId{id};
    }
    return UserProfilePage{query, p};
}
MusicResult listFrom(const UserProfileQuery& query, const QJsonObject& object, bool fans, int limit)
{
    const auto raw = object.value(fans ? QStringLiteral("followeds") : QStringLiteral("follow"));
    if (!raw.isArray())
        return invalidBecause("list");
    const auto rows = raw.toArray();
    if (rows.size() > limit)
        return invalidBecause("rows");
    UserList list;
    if (!boolean(object.value(QStringLiteral("more")), list.hasMore) ||
        (fans && !number(object.value(QStringLiteral("size")), list.total)))
        return invalidBecause("page");
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (!rows.at(i).isObject())
            return invalidBecause("row", i);
        const auto o = rows.at(i).toObject();
        UserSummary u;
        if (!personFrom(o, u.userId, u.nickname, u.avatar, u.signature, u.followed))
            return invalidBecause("user", i);
        if (fans) {
            std::optional<qint64> time;
            if (!number(o.value(QStringLiteral("time")), time) || !time || *time <= 0)
                return invalidBecause("time", i);
            if (list.nextCursor && *time > *list.nextCursor)
                return invalidBecause("order", i); // newest first; the cursor must go back
            list.nextCursor = time;
        }
        list.users.append(u);
    }
    if (!fans || list.users.isEmpty())
        list.nextCursor.reset();
    return UserProfilePage{query, list};
}
} // namespace

std::optional<UserProfileWire> userProfileRequest(const UserProfileQuery& query)
{
    if (!query.isValid())
        return std::nullopt;
    return std::visit(
        [](const auto& q) -> UserProfileWire {
            using T = std::decay_t<decltype(q)>;
            QJsonObject p{{QStringLiteral("uid"), q.userId}};
            if constexpr (std::is_same_v<T, UserDetailQuery>)
                return {ApiOperation::UserDetail, p};
            if constexpr (std::is_same_v<T, UserPlaylistsQuery> ||
                          std::is_same_v<T, UserFollowsQuery>) {
                p.insert(QStringLiteral("limit"), q.page.limit);
                p.insert(QStringLiteral("offset"), q.page.offset);
                return {std::is_same_v<T, UserPlaylistsQuery> ? ApiOperation::UserPlaylists
                                                              : ApiOperation::UserFollows,
                        p};
            }
            if constexpr (std::is_same_v<T, UserFansQuery>) {
                p.insert(QStringLiteral("limit"), q.limit);
                p.insert(QStringLiteral("time"), QString::number(q.cursor));
                return {ApiOperation::UserFans, p};
            }
            if constexpr (std::is_same_v<T, UserRankingQuery>) {
                p.insert(QStringLiteral("type"), q.period == RankingPeriod::LastWeek ? 1 : 0);
                return {ApiOperation::UserRanking, p};
            }
        },
        query.action);
}

bool userProfileHidden(const UserProfileQuery& query, int providerCode)
{
    if (std::holds_alternative<UserFollowsQuery>(query.action) ||
        std::holds_alternative<UserFansQuery>(query.action))
        return providerCode == 400;
    return std::holds_alternative<UserRankingQuery>(query.action) && providerCode == -2;
}

MusicResult mapUserProfileResponse(const UserProfileQuery& query, const QJsonObject& object)
{
    if (!query.isValid() || object.value(QStringLiteral("code")) != QJsonValue(200))
        return MusicFailure{MusicError::InvalidResponse};
    if (std::holds_alternative<UserDetailQuery>(query.action))
        return profileFrom(query, object);
    if (const auto* q = std::get_if<UserFollowsQuery>(&query.action))
        return listFrom(query, object, false, q->page.limit);
    if (const auto* q = std::get_if<UserFansQuery>(&query.action))
        return listFrom(query, object, true, q->limit);
    // Same responses as the account's own playlists and ranking: their mappers, with the
    // viewed user in place of the account.
    if (const auto* q = std::get_if<UserPlaylistsQuery>(&query.action)) {
        auto result = mapLibraryResponse(MyPlaylistsQuery{q->userId, q->page}, object);
        if (auto* page = std::get_if<MyPlaylistsPage>(&result))
            return UserProfilePage{query,
                                   UserPlaylists{page->playlists, page->hasMore, page->nextOffset}};
        return result;
    }
    const auto& q = std::get<UserRankingQuery>(query.action);
    auto result = mapCloudHistoryResponse(ListeningRankingQuery{q.userId, q.period}, object);
    if (auto* ranking = std::get_if<ListeningRanking>(&result))
        return UserProfilePage{query, UserRanking{ranking->songs}};
    return result;
}
} // namespace cirrus::netease
