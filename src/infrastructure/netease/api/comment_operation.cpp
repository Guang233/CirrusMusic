// Fixed api-enhanced comment_* request structures (MIT); see NOTICE.md.
#include "comment_operation.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
namespace cirrus::netease
{
namespace
{
struct Resource {
    QString prefix;
    int type;
};
std::optional<Resource> resource(const QString& kind)
{
    if (kind == "song")
        return Resource{QStringLiteral("R_SO_4_"), 4};
    if (kind == "playlist")
        return Resource{QStringLiteral("A_PL_0_"), 0};
    if (kind == "album")
        return Resource{QStringLiteral("R_AL_3_"), 3};
    if (kind == "program")
        return Resource{QStringLiteral("A_DJ_1_"), 1};
    if (kind == "podcast")
        return Resource{QStringLiteral("A_DR_14_"), 14};
    return std::nullopt;
}
QString ordinaryKind(ApiOperation op)
{
    switch (op) {
    case ApiOperation::SongComments:
        return QStringLiteral("song");
    case ApiOperation::PlaylistComments:
        return QStringLiteral("playlist");
    case ApiOperation::AlbumComments:
        return QStringLiteral("album");
    case ApiOperation::ProgramComments:
        return QStringLiteral("program");
    default:
        return {};
    }
}
bool integer(const QJsonValue& v, qint64 min, qint64 max)
{
    const auto n = v.toInteger(min - 1);
    return v.isDouble() && n >= min && n <= max && v == QJsonValue(n);
}
} // namespace
bool isCommentOperation(ApiOperation op)
{
    return !ordinaryKind(op).isEmpty() || op == ApiOperation::HotComments ||
           op == ApiOperation::SortedComments || op == ApiOperation::CommentFloor ||
           op == ApiOperation::CommentInfo;
}
QString commentKindName(const domain::CommentTarget& target)
{
    using K = domain::CommentResourceKind;
    switch (domain::commentResourceKind(target)) {
    case K::Song:
        return QStringLiteral("song");
    case K::Playlist:
        return QStringLiteral("playlist");
    case K::Album:
        return QStringLiteral("album");
    case K::Program:
        return QStringLiteral("program");
    case K::Podcast:
        return QStringLiteral("podcast");
    }
    return {};
}
QString commentThreadId(const domain::CommentTarget& target)
{
    if (!domain::validCommentTarget(target))
        return {};
    const auto r = resource(commentKindName(target));
    return r ? r->prefix + domain::commentTargetId(target) : QString{};
}
int commentResourceType(const domain::CommentTarget& target)
{
    const auto r = resource(commentKindName(target));
    return r ? r->type : -1;
}
std::optional<ApiOperationContract> commentOperationContract(ApiOperation op, const QJsonObject& p)
{
    if (!isCommentOperation(op))
        return std::nullopt;
    if (op == ApiOperation::SortedComments)
        return ApiOperationContract{QStringLiteral("/api/v2/resource/comments"), Protocol::Eapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    if (op == ApiOperation::CommentFloor)
        return ApiOperationContract{QStringLiteral("/api/resource/comment/floor/get"),
                                    Protocol::Weapi, ResponseEncoding::Json, 4 * 1024 * 1024};
    if (op == ApiOperation::CommentInfo)
        return ApiOperationContract{QStringLiteral("/api/resource/commentInfo/list"),
                                    Protocol::Weapi, ResponseEncoding::Json, 1024 * 1024};
    const auto kind =
        op == ApiOperation::HotComments ? p.value("kind").toString() : ordinaryKind(op);
    const auto r = resource(kind);
    const auto id = p.value("id").toString();
    if (!r || !domain::validMusicId(id))
        return std::nullopt;
    const auto path = op == ApiOperation::HotComments
                          ? QStringLiteral("/api/v1/resource/hotcomments/")
                          : QStringLiteral("/api/v1/resource/comments/");
    return ApiOperationContract{path + r->prefix + id, Protocol::Weapi, ResponseEncoding::Json,
                                4 * 1024 * 1024};
}
std::optional<QJsonObject> commentOperationPayload(ApiOperation op, const QJsonObject& p)
{
    if (!isCommentOperation(op))
        return std::nullopt;
    const auto fixed = ordinaryKind(op);
    const auto kind = fixed.isEmpty() ? p.value("kind").toString() : fixed;
    const auto r = resource(kind);
    if (!r)
        return std::nullopt;
    if (op == ApiOperation::CommentInfo) {
        const auto raw = p.value("ids");
        const auto ids = raw.toArray();
        if (p.size() != 2 || !raw.isArray() || ids.isEmpty() || ids.size() > 100)
            return std::nullopt;
        QSet<QString> seen;
        for (const auto v : ids) {
            const auto id = v.toString();
            if (!domain::validMusicId(id) || seen.contains(id))
                return std::nullopt;
            seen.insert(id);
        }
        return QJsonObject{{QStringLiteral("resourceType"), QString::number(r->type)},
                           {QStringLiteral("resourceIds"),
                            QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact))}};
    }
    const auto id = p.value("id").toString();
    const auto limit = p.value("limit");
    if (!domain::validMusicId(id) || !integer(limit, 1, 100))
        return std::nullopt;
    const auto thread = r->prefix + id;
    if (op == ApiOperation::SortedComments) {
        const auto page = p.value("page"), before = p.value("before"), inner = p.value("showInner");
        const auto order = p.value("order").toString();
        if (p.size() != 7 || !integer(page, 1, 10000) ||
            !integer(before, 0, domain::MaximumCommentCursor) || !inner.isBool())
            return std::nullopt;
        const auto offset = (page.toInteger() - 1) * limit.toInteger();
        if (offset > 100000)
            return std::nullopt;
        int sort = 0;
        QJsonValue cursor;
        if (order == "recommended" || order == "hot") {
            if (before.toInteger() != 0)
                return std::nullopt;
            sort = order == "hot" ? 2 : 99;
            cursor = sort == 99
                         ? QJsonValue(offset)
                         : QJsonValue(QStringLiteral("normalHot#") + QString::number(offset));
        } else if (order == "time") {
            if (page.toInteger() == 1 ? before.toInteger() != 0 : before.toInteger() <= 0)
                return std::nullopt;
            sort = 3;
            cursor = QString::number(before.toInteger());
        } else
            return std::nullopt;
        return QJsonObject{
            {QStringLiteral("threadId"), thread}, {QStringLiteral("pageNo"), page},
            {QStringLiteral("pageSize"), limit},  {QStringLiteral("cursor"), cursor},
            {QStringLiteral("sortType"), sort},   {QStringLiteral("showInner"), inner}};
    }
    if (op == ApiOperation::CommentFloor) {
        const auto parent = p.value("parent").toString();
        const auto time = p.value("time");
        if (p.size() != 5 || !domain::validMusicId(parent) ||
            !integer(time, -1, domain::MaximumCommentCursor))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("parentCommentId"), parent},
                           {QStringLiteral("threadId"), thread},
                           {QStringLiteral("time"), time},
                           {QStringLiteral("limit"), limit}};
    }
    const auto offset = p.value("offset"), before = p.value("before");
    if (p.size() != (fixed.isEmpty() ? 5 : 4) || !integer(offset, 0, 100000) ||
        !integer(before, 0, domain::MaximumCommentCursor))
        return std::nullopt;
    return QJsonObject{{QStringLiteral("rid"), id},
                       {QStringLiteral("limit"), limit},
                       {QStringLiteral("offset"), offset},
                       {QStringLiteral("beforeTime"), before}};
}
} // namespace cirrus::netease
