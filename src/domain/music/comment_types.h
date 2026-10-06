#pragma once
#include "podcast_types.h"
#include <QSet>
#include <type_traits>
#include <variant>

namespace cirrus::domain
{
// Logical resources only. No VoiceId/ArtistId/video/event alias or arbitrary thread string.
using CommentTarget = std::variant<SongId, PlaylistId, AlbumId, ProgramId, PodcastId>;
enum class CommentResourceKind { Song, Playlist, Album, Program, Podcast };
inline CommentResourceKind commentResourceKind(const CommentTarget& target)
{
    return std::visit(
        [](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, SongId>)
                return CommentResourceKind::Song;
            else if constexpr (std::is_same_v<T, PlaylistId>)
                return CommentResourceKind::Playlist;
            else if constexpr (std::is_same_v<T, AlbumId>)
                return CommentResourceKind::Album;
            else if constexpr (std::is_same_v<T, ProgramId>)
                return CommentResourceKind::Program;
            else
                return CommentResourceKind::Podcast;
        },
        target);
}
inline QString commentTargetId(const CommentTarget& target)
{
    return std::visit([](const auto& id) { return id.value; }, target);
}
inline bool validCommentTarget(const CommentTarget& target)
{
    return validMusicId(commentTargetId(target));
}
inline constexpr qint64 MaximumCommentCursor = 9007199254740991LL;
struct CommentId {
    QString value;
};
struct CommentPageRequest {
    int limit = 20, offset = 0;
    qint64 before = 0;
    [[nodiscard]] bool isValid() const
    {
        return limit >= 1 && limit <= 100 && offset >= 0 && offset <= 100000 && before >= 0 &&
               before <= MaximumCommentCursor;
    }
};
struct CommentsQuery {
    CommentTarget target;
    CommentPageRequest page;
    [[nodiscard]] bool isValid() const
    {
        return validCommentTarget(target) && page.isValid() &&
               commentResourceKind(target) != CommentResourceKind::Podcast;
    }
};
struct HotCommentsQuery {
    CommentTarget target;
    CommentPageRequest page;
    [[nodiscard]] bool isValid() const { return validCommentTarget(target) && page.isValid(); }
};
enum class CommentOrder { Recommended, Hot, Chronological };
struct SortedCommentsQuery {
    CommentTarget target;
    int limit = 20, page = 1;
    CommentOrder order = CommentOrder::Recommended;
    qint64 before = 0; // explicit prior last-row time only for chronological pages
    bool showInner = true;
    [[nodiscard]] bool isValid() const
    {
        return validCommentTarget(target) && limit >= 1 && limit <= 100 && page >= 1 &&
               page <= 10000 && qint64(page - 1) * limit <= 100000 && before >= 0 &&
               before <= MaximumCommentCursor &&
               ((order == CommentOrder::Chronological && (page == 1 ? before == 0 : before > 0)) ||
                ((order == CommentOrder::Recommended || order == CommentOrder::Hot) &&
                 before == 0));
    }
};
struct CommentFloorQuery {
    CommentTarget target;
    CommentId parent;
    int limit = 20;
    std::optional<qint64> time; // absent -> provider sentinel -1; explicit 0 stays 0
    [[nodiscard]] bool isValid() const
    {
        return validCommentTarget(target) && validMusicId(parent.value) && limit >= 1 &&
               limit <= 100 && (!time || (*time >= 0 && *time <= MaximumCommentCursor));
    }
};
struct CommentInfoQuery {
    QList<CommentTarget> targets; // one namespace, canonical, unique; no hidden dedup
    [[nodiscard]] bool isValid() const
    {
        if (targets.isEmpty() || targets.size() > 100)
            return false;
        const auto kind = commentResourceKind(targets.first());
        QSet<QString> ids;
        for (const auto& target : targets) {
            const auto id = commentTargetId(target);
            if (!validMusicId(id) || commentResourceKind(target) != kind || ids.contains(id))
                return false;
            ids.insert(id);
        }
        return true;
    }
};
struct CommentAuthor {
    std::optional<QString> providerUserId; // public author reference, NOT verified AccountId
    QString nickname;
    QUrl avatar;
};
struct CommentExcerpt {
    CommentId id;
    std::optional<QString> content; // plain untrusted text; missing/deleted is unknown
    std::optional<CommentAuthor> author;
    std::optional<qint64> providerTime; // raw provider value, not local date or duration
};
struct CommentQuote {
    std::optional<CommentId> id;
    std::optional<QString> content;
    std::optional<CommentAuthor> author;
};
struct Comment : CommentExcerpt {
    std::optional<qint64> likedCount, replyCount;
    std::optional<bool> liked; // original context only; never infers authentication
    std::optional<QList<CommentQuote>> quotes;
    std::optional<QList<CommentExcerpt>> innerReplies; // bounded nonrecursive display previews
};
struct CommentsPage {
    CommentsQuery query;
    QList<Comment> comments;
    std::optional<QList<Comment>> hotComments, topComments;
    std::optional<qint64> total, nextBefore;
    std::optional<bool> hasMore;
};
struct HotCommentsPage {
    HotCommentsQuery query;
    QList<Comment> comments;
    std::optional<qint64> total, nextBefore;
    std::optional<bool> hasMore;
};
struct SortedCommentsPage {
    SortedCommentsQuery query;
    QList<Comment> comments;
    std::optional<qint64> total, nextBefore;
    std::optional<bool> hasMore;
    std::optional<QString> providerCursor; // raw, not automatically an accepted next query
};
struct CommentFloorPage {
    CommentFloorQuery query;
    QList<Comment> comments;
    std::optional<Comment> owner;
    std::optional<qint64> total, providerTime, nextTime;
    std::optional<bool> hasMore;
};
struct CommentStatistic {
    CommentTarget target;
    std::optional<qint64> commentCount, likedCount, shareCount, musicianSaidCount;
    std::optional<QString> countDescription; // display text, not a parsed exact count
    std::optional<bool> liked, upgraded;
};
struct CommentStatistics {
    CommentInfoQuery query;
    QList<CommentStatistic> items; // provider order; absent requests remain unknown, never zero
    QList<CommentTarget> missing;
};
} // namespace cirrus::domain
