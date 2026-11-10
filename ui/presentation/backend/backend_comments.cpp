#include "backend_comments.h"

#include "backend_mapping.h"
#include "comment_writes.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "domain/music/comment_types.h"
#include "domain/music/comment_write_types.h"
#include "infrastructure/runtime/backend_runtime.h"

namespace cirrus::ui
{
namespace
{

using namespace mapping;

domain::CommentTarget targetOf(const CommentTarget& target)
{
    switch (target.kind) {
    case comment::Playlist:
        return domain::PlaylistId{target.key};
    case comment::Album:
        return domain::AlbumId{target.key};
    case comment::Song:
        break;
    }
    return domain::SongId{target.key};
}

CommentInfo commentOf(const domain::Comment& comment)
{
    CommentInfo info;
    info.key = comment.id.value;
    if (comment.author) {
        info.authorKey = comment.author->providerUserId.value_or(QString());
        info.authorName = comment.author->nickname;
        info.avatarKey = coverKey(comment.author->avatar);
    }
    info.text = comment.content;
    info.timeMs = comment.providerTime.value_or(0);
    info.likedCount = comment.likedCount.value_or(-1);
    info.liked = comment.liked.value_or(false);
    info.replyCount = comment.replyCount.value_or(-1);
    // The comment replied to, as the service quotes it.
    if (comment.quotes && !comment.quotes->isEmpty()) {
        const auto& quote = comment.quotes->constFirst();
        info.replyTo = CommentExcerpt{quote.author ? quote.author->nickname : QString(),
                                      quote.content.value_or(QString())};
    }
    return info;
}

QList<CommentInfo> commentsOf(const QList<domain::Comment>& comments)
{
    QList<CommentInfo> items;
    items.reserve(comments.size());
    for (const auto& comment : comments)
        items << commentOf(comment);
    return items;
}

// The list's place: its page, and for newest first the last comment's time.
struct ListCursor {
    int page = 1;
    qint64 before = 0;
};

std::optional<ListCursor> listCursor(const QString& cursor)
{
    if (cursor.isEmpty())
        return ListCursor{};
    const auto parts = cursor.split(u':');
    bool pageOk = false, beforeOk = true;
    ListCursor result;
    result.page = parts.value(0).toInt(&pageOk);
    if (parts.size() > 1)
        result.before = parts.at(1).toLongLong(&beforeOk);
    if (!pageOk || !beforeOk || parts.size() > 2 || result.page < 1 || result.before < 0)
        return std::nullopt;
    return result;
}

domain::CommentOrder orderOf(comment::Sort sort)
{
    switch (sort) {
    case comment::Hot:
        return domain::CommentOrder::Hot;
    case comment::Newest:
        return domain::CommentOrder::Chronological;
    case comment::Recommended:
        break;
    }
    return domain::CommentOrder::Recommended;
}

CommentPage emptyPage() { return CommentPage{{}, false, {}, {}}; }

} // namespace

BackendComments::BackendComments(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_requests(new MusicRequests(runtime.facade(), this)),
      m_writes(new CommentWrites(runtime.facade(), this))
{
}

BackendComments::~BackendComments() = default;

Request BackendComments::comments(const CommentTarget& target, comment::Sort sort,
                                  const QString& cursor, int limit, Reply<CommentPage> reply)
{
    const auto place = listCursor(cursor);
    domain::SortedCommentsQuery query;
    query.target = targetOf(target);
    query.limit = std::clamp(limit, 1, 100);
    query.order = orderOf(sort);
    if (place) {
        query.page = place->page;
        query.before = query.order == domain::CommentOrder::Chronological ? place->before : 0;
    }
    if (!place || !query.isValid())
        return replyLater(this, std::move(reply), Outcome<CommentPage>{emptyPage()});
    return read<domain::SortedCommentsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.sortedComments(query); },
        std::move(reply),
        [](const domain::SortedCommentsPage& found) {
            CommentPage page;
            page.items = commentsOf(found.comments);
            page.total = found.total;
            page.hasMore = found.hasMore;
            const auto& q = found.query;
            const bool more = found.hasMore.value_or(found.comments.size() >= q.limit) &&
                              !found.comments.isEmpty();
            domain::SortedCommentsQuery next = q;
            next.page = q.page + 1;
            if (q.order == domain::CommentOrder::Chronological)
                next.before = found.nextBefore.value_or(0);
            // No next page the backend would take (newest without a last time, or past
            // the deepest page): the list ends here.
            if (more && next.isValid())
                page.next = q.order == domain::CommentOrder::Chronological
                                ? QStringLiteral("%1:%2").arg(next.page).arg(next.before)
                                : QString::number(next.page);
            if (page.next.isEmpty())
                page.hasMore = false;
            return page;
        });
}

Request BackendComments::replies(const CommentTarget& target, const QString& commentKey,
                                 const QString& cursor, int limit, Reply<CommentPage> reply)
{
    domain::CommentFloorQuery query;
    query.target = targetOf(target);
    query.parent = domain::CommentId{commentKey};
    query.limit = std::clamp(limit, 1, 100);
    bool ok = true;
    if (!cursor.isEmpty())
        query.time = cursor.toLongLong(&ok);
    if (!ok || !query.isValid())
        return replyLater(this, std::move(reply), Outcome<CommentPage>{emptyPage()});
    return read<domain::CommentFloorPage>(
        m_requests, [query](BackendFacade& facade) { return facade.commentFloor(query); },
        std::move(reply),
        [](const domain::CommentFloorPage& found) {
            CommentPage page;
            page.items = commentsOf(found.comments);
            page.total = found.total;
            page.hasMore = found.hasMore;
            const bool more = found.hasMore.value_or(found.comments.size() >= found.query.limit) &&
                              !found.comments.isEmpty();
            // The next replies start after the last one's time; the same time again
            // would read the same page.
            if (more && found.nextTime && *found.nextTime > 0 && found.nextTime != found.query.time)
                page.next = QString::number(*found.nextTime);
            if (page.next.isEmpty())
                page.hasMore = false;
            return page;
        });
}

namespace
{
// A write's reply: `value` once acknowledged, the failure otherwise.
Request write(CommentWrites* writes, domain::CommentChange change, Reply<bool> reply,
              bool value = true)
{
    return writes->submit(
        std::move(change), [reply, value]() { reply(Outcome<bool>{value}); },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply(Outcome<bool>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}
} // namespace

Request BackendComments::setCommentLiked(const CommentTarget& target, const QString& commentKey,
                                         bool liked, Reply<bool> reply)
{
    return write(m_writes,
                 domain::CommentReaction{targetOf(target), domain::CommentId{commentKey}, liked},
                 std::move(reply), liked);
}

Request BackendComments::publishComment(const CommentTarget& target, const QString& text,
                                        Reply<bool> reply)
{
    return write(m_writes, domain::CommentPublish{targetOf(target), text}, std::move(reply));
}

Request BackendComments::replyToComment(const CommentTarget& target, const QString& commentKey,
                                        const QString& text, Reply<bool> reply)
{
    return write(m_writes,
                 domain::CommentReply{targetOf(target), domain::CommentId{commentKey}, text},
                 std::move(reply));
}

Request BackendComments::deleteComment(const CommentTarget& target, const QString& commentKey,
                                       Reply<bool> reply)
{
    return write(m_writes, domain::CommentDelete{targetOf(target), domain::CommentId{commentKey}},
                 std::move(reply));
}

} // namespace cirrus::ui
