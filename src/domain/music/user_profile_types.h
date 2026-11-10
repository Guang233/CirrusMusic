#pragma once
#include "cloud_history_types.h"
#include "library_types.h"
#include <variant>

namespace cirrus::domain
{
// Any user's public profile (docs/api/user-profile-reads.md). Unlike the account library,
// the user is the caller's choice; reads need no signed-in account. The provider's
// privacy settings hide lists and rankings (UserProfileHidden), never the profile itself.
struct UserDetailQuery {
    QString userId;
    [[nodiscard]] bool isValid() const { return validMusicId(userId); }
};
struct UserPlaylistsQuery {
    QString userId;
    CatalogPage page{100, 0};
    [[nodiscard]] bool isValid() const { return validMusicId(userId) && page.isValid(); }
};
struct UserFollowsQuery {
    QString userId;
    CatalogPage page;
    [[nodiscard]] bool isValid() const { return validMusicId(userId) && page.isValid(); }
};
// Fans page by follow time: 0 for the first page, then the previous page's nextCursor.
struct UserFansQuery {
    QString userId;
    int limit = 30;
    qint64 cursor = 0;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(userId) && limit >= 1 && limit <= 100 && cursor >= 0 &&
               cursor <= 9999999999999;
    }
};
struct UserRankingQuery {
    QString userId;
    RankingPeriod period = RankingPeriod::AllTime;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(userId) &&
               (period == RankingPeriod::AllTime || period == RankingPeriod::LastWeek);
    }
};
using UserProfileAction = std::variant<UserDetailQuery, UserPlaylistsQuery, UserFollowsQuery,
                                       UserFansQuery, UserRankingQuery>;
struct UserProfileQuery {
    UserProfileAction action;
    [[nodiscard]] bool isValid() const
    {
        return std::visit([](const auto& q) { return q.isValid(); }, action);
    }
    [[nodiscard]] QString userId() const
    {
        return std::visit([](const auto& q) { return q.userId; }, action);
    }
};

struct UserProfile {
    QString userId;
    QString nickname;
    QUrl avatar;
    QString signature;
    std::optional<qint64> level, follows, fans, playlistCount;
    std::optional<bool> followed;    // by the signed-in account; unknown for guests
    std::optional<ArtistId> artist;  // musicians only: their artist page
    std::optional<bool> rankingOpen; // listening ranking visible to others
};
struct UserSummary {
    QString userId;
    QString nickname;
    QUrl avatar;
    QString signature;
    std::optional<bool> followed;
};
struct UserList {
    QList<UserSummary> users;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
    std::optional<qint64> nextCursor; // fans only: the last row's follow time
};
struct UserPlaylists {
    QList<LibraryPlaylist> playlists; // created and saved together; creatorId tells them apart
    std::optional<bool> hasMore;
};
struct UserRanking {
    QList<RankedSong> songs;
};
// The user hides this list or ranking (follows/fans: code 400, ranking: code -2).
struct UserProfileHidden {};
struct UserProfilePage {
    UserProfileQuery query;
    std::variant<UserProfile, UserPlaylists, UserList, UserRanking, UserProfileHidden> content;
};
// The content kind each query answers with; hidden is allowed except for the profile.
inline bool matchingUserProfilePage(const UserProfileQuery& query, const UserProfilePage& page)
{
    const auto kind = query.action.index();
    if (kind != page.query.action.index() || query.userId() != page.query.userId())
        return false;
    const auto content = page.content.index();
    const std::size_t expected = kind == 0 ? 0 : kind == 1 ? 1 : kind == 4 ? 3 : 2;
    return content == expected ||
           (kind != 0 && std::holds_alternative<UserProfileHidden>(page.content));
}
} // namespace cirrus::domain
