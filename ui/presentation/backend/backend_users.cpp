// BackendCatalog: users' pages (docs/ui/pages/user.md, docs/api/user-profile-reads.md).
// Guests read them too; a list or ranking the user hides arrives as `hidden`.

#include "backend_catalog.h"
#include "backend_mapping.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "domain/music/user_profile_types.h"

#include <QSet>

namespace cirrus::ui
{
namespace
{

using namespace mapping;

// The service's most per page.
constexpr int kPage = 100;
// A user's playlists come a page at a time, up to this many.
constexpr int kMaxPlaylists = 1000;

CollectionItem userPlaylistOf(const domain::LibraryPlaylist& playlist, const QString& user)
{
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
        item.created = *playlist.creatorId == user;
    return item;
}

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
    if (!domain::UserProfileQuery{domain::UserPlaylistsQuery{key}}.isValid())
        return rejectLater(this, std::move(reply));
    // Page after page until the last; the page shows them all at once.
    struct Fetch {
        QString key;
        QList<CollectionItem> items;
        QSet<QString> keys;
        Request current;
        Reply<QList<CollectionItem>> reply;
    };
    auto fetch = std::make_shared<Fetch>();
    fetch->key = key;
    fetch->reply = std::move(reply);
    auto next = std::make_shared<std::function<void(int)>>();
    *next = [this, weak = std::weak_ptr<Fetch>(fetch),
             weakNext = std::weak_ptr<std::function<void(int)>>(next)](int offset) {
        const auto fetch = weak.lock();
        if (!fetch)
            return;
        const domain::UserProfileQuery query{
            domain::UserPlaylistsQuery{fetch->key, {kPage, offset}}};
        fetch->current = read<domain::UserProfilePage>(
            m_requests, [query](BackendFacade& facade) { return facade.userProfile(query); },
            Reply<domain::UserPlaylists>(
                [weak, weakNext, offset](Outcome<domain::UserPlaylists> page) {
                    const auto fetch = weak.lock();
                    const auto next = weakNext.lock();
                    if (!fetch || !next || !fetch->reply)
                        return;
                    fetch->current.release(); // done; the next page replaces it
                    if (!page.value) {
                        // A later page failing keeps what came; the first fails the read.
                        auto done = std::exchange(fetch->reply, {});
                        if (offset == 0)
                            done({std::nullopt, false, page.failure});
                        else
                            done({fetch->items, true, std::nullopt});
                        return;
                    }
                    // A page's rows may repeat the first page's extra ones; each once.
                    for (const auto& playlist : page.value->playlists) {
                        if (fetch->keys.contains(playlist.id.value))
                            continue;
                        fetch->keys.insert(playlist.id.value);
                        fetch->items.append(userPlaylistOf(playlist, fetch->key));
                    }
                    // The service's offset, not rows read: its first page has rows it
                    // does not count.
                    const int nextOffset = page.value->nextOffset;
                    if (page.value->hasMore == true && !page.value->playlists.isEmpty() &&
                        nextOffset > offset && nextOffset < kMaxPlaylists)
                        (*next)(nextOffset);
                    else
                        std::exchange(fetch->reply, {})({fetch->items, false, std::nullopt});
                }),
            [](const domain::UserProfilePage& page) {
                const auto* found = std::get_if<domain::UserPlaylists>(&page.content);
                return found ? *found : domain::UserPlaylists{};
            });
    };
    (*next)(0);
    // The fetch (and with it the step function) lives as long as the request.
    return Request([fetch, next] { fetch->current.cancel(); });
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
