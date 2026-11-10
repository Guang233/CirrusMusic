// BackendCatalog: users' pages (docs/ui/pages/user.md, docs/api/user-profile-reads.md).
// Guests read them too; a list or ranking the user hides arrives as `hidden`.

#include "backend_catalog.h"
#include "backend_mapping.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "domain/music/user_profile_types.h"

namespace cirrus::ui
{
namespace
{

using namespace mapping;

// The service's most per page; a user's playlists come in one read of this many.
constexpr int kPage = 100;

UserItem userItemOf(const domain::UserSummary& user)
{
    return {user.userId, user.nickname, coverKey(user.avatar), user.signature,
            user.followed.value_or(false)};
}

UserList userListOf(const domain::UserProfilePage& page)
{
    UserList list;
    if (std::holds_alternative<domain::UserProfileHidden>(page.content)) {
        list.hidden = true;
        list.page.hasMore = false;
        return list;
    }
    const auto* users = std::get_if<domain::UserList>(&page.content);
    if (!users)
        return list;
    for (const auto& user : users->users)
        list.page.items.append(userItemOf(user));
    list.page.hasMore = users->hasMore;
    list.page.total = users->total;
    list.page.next = users->nextCursor;
    return list;
}

template <class T> Request rejectLater(QObject* context, Reply<T> reply)
{
    return replyLater(context, std::move(reply),
                      Outcome<T>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
}

} // namespace

Request BackendCatalog::user(const QString& key, Reply<UserInfo> reply)
{
    const domain::UserProfileQuery query{domain::UserDetailQuery{key}};
    if (!query.isValid())
        return rejectLater(this, std::move(reply));
    return read<domain::UserProfilePage>(
        m_requests, [query](BackendFacade& facade) { return facade.userProfile(query); },
        std::move(reply),
        [](const domain::UserProfilePage& page) {
            UserInfo info;
            const auto* p = std::get_if<domain::UserProfile>(&page.content);
            if (!p)
                return info;
            info.key = p->userId;
            info.nickname = p->nickname;
            info.avatarKey = coverKey(p->avatar);
            info.signature = p->signature;
            info.level = p->level ? int(*p->level) : -1;
            info.follows = p->follows.value_or(-1);
            info.fans = p->fans.value_or(-1);
            info.playlists = p->playlistCount.value_or(-1);
            info.artistKey = p->artist ? p->artist->value : QString();
            info.rankingOpen = p->rankingOpen.value_or(true);
            info.followed = p->followed.value_or(false);
            return info;
        });
}

Request BackendCatalog::userPlaylists(const QString& key, Reply<QList<CollectionItem>> reply)
{
    const domain::UserProfileQuery query{domain::UserPlaylistsQuery{key, {kPage, 0}}};
    if (!query.isValid())
        return rejectLater(this, std::move(reply));
    return read<domain::UserProfilePage>(
        m_requests, [query](BackendFacade& facade) { return facade.userProfile(query); },
        std::move(reply),
        [key](const domain::UserProfilePage& page) {
            QList<CollectionItem> items;
            const auto* found = std::get_if<domain::UserPlaylists>(&page.content);
            if (!found)
                return items;
            for (const auto& playlist : found->playlists) {
                CollectionItem item;
                item.key = playlist.id.value;
                item.kind = collection::Playlist;
                item.title = playlist.title;
                item.count = playlist.trackCount ? int(*playlist.trackCount) : -1;
                item.coverKey = coverKey(playlist.cover);
                // Its liked-songs playlist is its own; otherwise by the creator.
                if (playlist.accountLikedSongs == true)
                    item.created = true;
                else if (playlist.creatorId)
                    item.created = *playlist.creatorId == key;
                items.append(item);
            }
            return items;
        });
}

Request BackendCatalog::userRanking(const QString& key, bool allTime, Reply<UserRanking> reply)
{
    const domain::UserProfileQuery query{domain::UserRankingQuery{
        key, allTime ? domain::RankingPeriod::AllTime : domain::RankingPeriod::LastWeek}};
    if (!query.isValid())
        return rejectLater(this, std::move(reply));
    return readWithCovers<domain::UserProfilePage>(
        m_requests, [query](BackendFacade& facade) { return facade.userProfile(query); },
        std::move(reply),
        [](const domain::UserProfilePage& page) {
            UserRanking ranking;
            ranking.hidden = std::holds_alternative<domain::UserProfileHidden>(page.content);
            if (const auto* found = std::get_if<domain::UserRanking>(&page.content))
                for (const auto& ranked : found->songs)
                    ranking.songs.append(songOf(ranked.song));
            return ranking;
        },
        [](UserRanking& ranking) { return &ranking.songs; });
}

Request BackendCatalog::userFollows(const QString& key, PageRequest page, Reply<UserList> reply)
{
    const domain::UserProfileQuery query{
        domain::UserFollowsQuery{key, {std::clamp(page.limit, 1, kPage), page.offset}}};
    if (!query.isValid())
        return rejectLater(this, std::move(reply));
    return read<domain::UserProfilePage>(
        m_requests, [query](BackendFacade& facade) { return facade.userProfile(query); },
        std::move(reply), userListOf);
}

Request BackendCatalog::userFans(const QString& key, qint64 before, int limit,
                                 Reply<UserList> reply)
{
    const domain::UserProfileQuery query{
        domain::UserFansQuery{key, std::clamp(limit, 1, kPage), before}};
    if (!query.isValid())
        return rejectLater(this, std::move(reply));
    return read<domain::UserProfilePage>(
        m_requests, [query](BackendFacade& facade) { return facade.userProfile(query); },
        std::move(reply), userListOf);
}

} // namespace cirrus::ui
