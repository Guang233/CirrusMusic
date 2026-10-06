#include "comment_mapper.h"
#include "infrastructure/netease/api/comment_operation.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isNull() || v.isUndefined(); }
bool number(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
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
bool echo(const QJsonObject& o, const CommentTarget& target)
{
    const auto thread = o.value("threadId"), id = o.value("resourceId"),
               kind = o.value("resourceType");
    return (absent(thread) || thread == QJsonValue(commentThreadId(target))) &&
           (absent(id) || providerId(id) == commentTargetId(target)) &&
           (absent(kind) ||
            nonnegative(kind) == std::optional<qint64>(commentResourceType(target)));
}
bool author(const QJsonValue& v, std::optional<CommentAuthor>& out)
{
    if (absent(v))
        return true;
    if (!v.isObject())
        return false;
    const auto o = v.toObject();
    CommentAuthor a;
    const auto uid = o.value("userId"), nickname = o.value("nickname");
    const auto id = providerId(uid);
    if ((!unknownId(uid) && id.isEmpty()) || !textField(nickname))
        return false;
    if (!id.isEmpty())
        a.providerUserId = id;
    a.nickname = nickname.toString();
    a.avatar = imageFrom(o.value("avatarUrl"));
    out = a;
    return true;
}
bool excerpt(const QJsonObject& o, CommentExcerpt& c, const CommentTarget& target)
{
    c.id = {providerId(o.value("commentId"))};
    return !c.id.value.isEmpty() && echo(o, target) && text(o.value("content"), c.content) &&
           author(o.value("user"), c.author) && number(o.value("time"), c.providerTime);
}
std::optional<Comment> comment(const QJsonObject& o, const CommentTarget& target)
{
    Comment c;
    if (!excerpt(o, c, target) || !number(o.value("likedCount"), c.likedCount) ||
        !boolean(o.value("liked"), c.liked))
        return std::nullopt;
    const auto quotes = o.value("beReplied");
    if (!absent(quotes)) {
        if (!quotes.isArray() || quotes.toArray().size() > 10)
            return std::nullopt;
        c.quotes = QList<CommentQuote>{};
        for (const auto v : quotes.toArray()) {
            if (!v.isObject())
                return std::nullopt;
            const auto q = v.toObject();
            CommentQuote row;
            const auto id = q.value("beRepliedCommentId");
            if (!unknownId(id)) {
                const auto valid = providerId(id);
                if (valid.isEmpty())
                    return std::nullopt;
                row.id = CommentId{valid};
            }
            if (!text(q.value("content"), row.content) || !author(q.value("user"), row.author))
                return std::nullopt;
            c.quotes->append(row);
        }
    }
    const auto floor = o.value("showFloorComment");
    if (!absent(floor)) {
        if (!floor.isObject())
            return std::nullopt;
        const auto f = floor.toObject();
        if (!number(f.value("replyCount"), c.replyCount))
            return std::nullopt;
        const auto values = f.value("comments");
        if (!absent(values)) {
            if (!values.isArray() || values.toArray().size() > 10 ||
                (c.replyCount && *c.replyCount < values.toArray().size()))
                return std::nullopt;
            c.innerReplies = QList<CommentExcerpt>{};
            for (const auto v : values.toArray()) {
                if (!v.isObject())
                    return std::nullopt;
                CommentExcerpt row;
                if (!excerpt(v.toObject(), row, target))
                    return std::nullopt;
                c.innerReplies->append(row);
            }
        }
    }
    return c;
}
bool rows(const QJsonValue& v, const CommentTarget& target, int limit, QList<Comment>& out)
{
    if (!v.isArray() || v.toArray().size() > limit)
        return false;
    for (const auto x : v.toArray()) {
        if (!x.isObject())
            return false;
        const auto c = comment(x.toObject(), target);
        if (!c)
            return false;
        out.append(*c);
    }
    return true;
}
bool optionalRows(const QJsonValue& v, const CommentTarget& target,
                  std::optional<QList<Comment>>& out)
{
    if (absent(v))
        return true;
    out = QList<Comment>{};
    return rows(v, target, 100, *out);
}
std::optional<qint64> lastTime(const QList<Comment>& comments)
{
    return comments.isEmpty() ? std::nullopt : comments.last().providerTime;
}
QJsonObject targetParameters(const CommentTarget& target)
{
    return {{QStringLiteral("kind"), commentKindName(target)},
            {QStringLiteral("id"), commentTargetId(target)}};
}
QJsonObject pageParameters(const CommentTarget& target, const CommentPageRequest& page)
{
    auto p = targetParameters(target);
    p.insert("limit", page.limit);
    p.insert("offset", page.offset);
    p.insert("before", page.before);
    return p;
}
} // namespace
std::optional<CommentRequest> commentRequest(const MusicQuery& query)
{
    if (!isCommentQuery(query) || !validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<CommentsQuery>(&query)) {
        auto p = pageParameters(q->target, q->page);
        p.remove("kind");
        ApiOperation op;
        switch (commentResourceKind(q->target)) {
        case CommentResourceKind::Song:
            op = ApiOperation::SongComments;
            break;
        case CommentResourceKind::Playlist:
            op = ApiOperation::PlaylistComments;
            break;
        case CommentResourceKind::Album:
            op = ApiOperation::AlbumComments;
            break;
        case CommentResourceKind::Program:
            op = ApiOperation::ProgramComments;
            break;
        default:
            return std::nullopt;
        }
        return CommentRequest{op, p};
    }
    if (const auto* q = std::get_if<HotCommentsQuery>(&query))
        return CommentRequest{ApiOperation::HotComments, pageParameters(q->target, q->page)};
    if (const auto* q = std::get_if<SortedCommentsQuery>(&query)) {
        auto p = targetParameters(q->target);
        p.insert("limit", q->limit);
        p.insert("page", q->page);
        p.insert("before", q->before);
        p.insert("order", q->order == CommentOrder::Recommended ? "recommended"
                          : q->order == CommentOrder::Hot       ? "hot"
                                                                : "time");
        p.insert("showInner", q->showInner);
        return CommentRequest{ApiOperation::SortedComments, p};
    }
    if (const auto* q = std::get_if<CommentFloorQuery>(&query)) {
        auto p = targetParameters(q->target);
        p.insert("parent", q->parent.value);
        p.insert("limit", q->limit);
        p.insert("time", q->time.value_or(-1));
        return CommentRequest{ApiOperation::CommentFloor, p};
    }
    const auto& q = std::get<CommentInfoQuery>(query);
    QJsonArray ids;
    for (const auto& t : q.targets)
        ids.append(commentTargetId(t));
    return CommentRequest{ApiOperation::CommentInfo,
                          {{QStringLiteral("kind"), commentKindName(q.targets.first())},
                           {QStringLiteral("ids"), ids}}};
}
MusicResult mapCommentResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!isCommentQuery(query) || !validMusicQuery(query) ||
        object.value("code") != QJsonValue(200))
        return invalid;
    if (const auto* q = std::get_if<CommentInfoQuery>(&query)) {
        const auto raw = object.value("data");
        if (!raw.isArray() || raw.toArray().size() > q->targets.size())
            return invalid;
        CommentStatistics page;
        page.query = *q;
        QSet<QString> seen;
        for (const auto v : raw.toArray()) {
            if (!v.isObject())
                return invalid;
            const auto o = v.toObject();
            const auto id = providerId(o.value("resourceId"));
            const auto it = std::find_if(q->targets.cbegin(), q->targets.cend(),
                                         [&](const auto& t) { return commentTargetId(t) == id; });
            if (it == q->targets.cend() || seen.contains(id) || !echo(o, *it))
                return invalid;
            seen.insert(id);
            CommentStatistic c;
            c.target = *it;
            if (!number(o.value("commentCount"), c.commentCount) ||
                !number(o.value("likedCount"), c.likedCount) ||
                !number(o.value("shareCount"), c.shareCount) ||
                !number(o.value("musicianSaidCount"), c.musicianSaidCount) ||
                !text(o.value("commentCountDesc"), c.countDescription) ||
                !boolean(o.value("liked"), c.liked) ||
                !boolean(o.value("commentUpgraded"), c.upgraded))
                return invalid;
            page.items.append(c);
        }
        for (const auto& t : q->targets)
            if (!seen.contains(commentTargetId(t)))
                page.missing.append(t);
        return page;
    }
    if (const auto* q = std::get_if<CommentsQuery>(&query)) {
        CommentsPage p;
        p.query = *q;
        if (!echo(object, q->target) ||
            !rows(object.value("comments"), q->target, q->page.limit, p.comments) ||
            !optionalRows(object.value("hotComments"), q->target, p.hotComments) ||
            !optionalRows(object.value("topComments"), q->target, p.topComments) ||
            !number(object.value("total"), p.total) || (p.total && *p.total < p.comments.size()) ||
            !boolean(object.value("more"), p.hasMore))
            return invalid;
        p.nextBefore = lastTime(p.comments);
        return p;
    }
    if (const auto* q = std::get_if<HotCommentsQuery>(&query)) {
        HotCommentsPage p;
        p.query = *q;
        if (!echo(object, q->target) ||
            !rows(object.value("hotComments"), q->target, q->page.limit, p.comments) ||
            !number(object.value("total"), p.total) || (p.total && *p.total < p.comments.size()) ||
            !boolean(object.value("more"), p.hasMore))
            return invalid;
        p.nextBefore = lastTime(p.comments);
        return p;
    }
    const auto raw = object.value("data");
    if (!raw.isObject())
        return invalid;
    const auto data = raw.toObject();
    if (const auto* q = std::get_if<SortedCommentsQuery>(&query)) {
        SortedCommentsPage p;
        p.query = *q;
        if (!echo(object, q->target) || !echo(data, q->target) ||
            !rows(data.value("comments"), q->target, q->limit, p.comments) ||
            !number(data.value("totalCount"), p.total) ||
            (p.total && *p.total < p.comments.size()) ||
            !boolean(data.value("hasMore"), p.hasMore) ||
            !text(data.value("cursor"), p.providerCursor))
            return invalid;
        if (p.providerCursor) {
            if (p.providerCursor->size() > 128)
                return invalid;
            for (const auto ch : *p.providerCursor)
                if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
                    return invalid;
        }
        const auto sort = data.value("sortType");
        const int expected = q->order == CommentOrder::Recommended ? 99
                             : q->order == CommentOrder::Hot       ? 2
                                                                   : 3;
        if (!absent(sort) && sort != QJsonValue(expected))
            return invalid;
        p.nextBefore = lastTime(p.comments);
        return p;
    }
    const auto& q = std::get<CommentFloorQuery>(query);
    CommentFloorPage p;
    p.query = q;
    if (!echo(object, q.target) || !echo(data, q.target) ||
        !rows(data.value("comments"), q.target, q.limit, p.comments) ||
        !number(data.value("totalCount"), p.total) || (p.total && *p.total < p.comments.size()) ||
        !number(data.value("time"), p.providerTime) || !boolean(data.value("hasMore"), p.hasMore))
        return invalid;
    const auto parent = data.value("parentCommentId");
    if (!absent(parent) && providerId(parent) != q.parent.value)
        return invalid;
    const auto owner = data.value("ownerComment");
    if (!absent(owner)) {
        if (!owner.isObject())
            return invalid;
        p.owner = comment(owner.toObject(), q.target);
        if (!p.owner || p.owner->id.value != q.parent.value)
            return invalid;
    }
    p.nextTime = lastTime(p.comments);
    return p;
}
} // namespace cirrus::netease
