#include "comment_write_operation.h"
#include "domain/music/comment_write_types.h"
namespace cirrus::netease
{
bool isCommentWriteOperation(ApiOperation op)
{
    return op == ApiOperation::CommentLike || op == ApiOperation::CommentUnlike ||
           op == ApiOperation::CommentPublish || op == ApiOperation::CommentReply ||
           op == ApiOperation::CommentDelete;
}
bool requiresCommentV3(ApiOperation op)
{
    return op == ApiOperation::CommentPublish || op == ApiOperation::CommentReply;
}
std::optional<ApiOperationContract> commentWriteContract(ApiOperation op)
{
    QString path;
    switch (op) {
    case ApiOperation::CommentLike:
        path = QStringLiteral("/api/v1/comment/like");
        break;
    case ApiOperation::CommentUnlike:
        path = QStringLiteral("/api/v1/comment/unlike");
        break;
    case ApiOperation::CommentPublish:
        path = QStringLiteral("/api/resource/comments/add");
        break;
    case ApiOperation::CommentReply:
        path = QStringLiteral("/api/v1/resource/comments/reply");
        break;
    case ApiOperation::CommentDelete:
        path = QStringLiteral("/api/resource/comments/delete");
        break;
    default:
        return std::nullopt;
    }
    return ApiOperationContract{path,
                                op == ApiOperation::CommentLike || op == ApiOperation::CommentUnlike
                                    ? Protocol::Weapi
                                    : Protocol::Xeapi,
                                op == ApiOperation::CommentLike || op == ApiOperation::CommentUnlike
                                    ? ResponseEncoding::Json
                                    : ResponseEncoding::EapiAutoJson,
                                64 * 1024};
}
std::optional<QJsonObject> commentWritePayload(ApiOperation op, const QJsonObject& p)
{
    if (!isCommentWriteOperation(op))
        return std::nullopt;
    const auto kind = p.value(QStringLiteral("kind"));
    const auto id = p.value(QStringLiteral("id"));
    const auto comment = p.value(QStringLiteral("comment"));
    const auto content = p.value(QStringLiteral("content"));
    const int k = kind.toInt(-1);
    const bool posting = requiresCommentV3(op), publishing = op == ApiOperation::CommentPublish;
    if (kind != QJsonValue(k) || k < 0 || k > (posting ? 3 : 4) || !id.isString() ||
        !domain::validMusicId(id.toString()) ||
        p.size() != (publishing ? 3
                     : posting  ? 4
                                : 3) ||
        (!publishing && (!comment.isString() || !domain::validMusicId(comment.toString()))) ||
        (posting && (!content.isString() || !domain::validCommentWriteText(content.toString()))))
        return std::nullopt;
    const QString prefixes[]{QStringLiteral("R_SO_4_"), QStringLiteral("A_PL_0_"),
                             QStringLiteral("R_AL_3_"), QStringLiteral("A_DJ_1_"),
                             QStringLiteral("A_DR_14_")};
    QJsonObject out{{QStringLiteral("threadId"), prefixes[k] + id.toString()}};
    if (!publishing)
        out.insert(QStringLiteral("commentId"), comment);
    if (posting) {
        out.insert(QStringLiteral("content"), content);
        out.insert(QStringLiteral("resourceType"), QStringLiteral("0"));
        if (publishing) {
            out.insert(QStringLiteral("expressionPicId"), QStringLiteral("-1"));
            out.insert(QStringLiteral("bubbleId"), QStringLiteral("-1"));
        }
    }
    return out;
}
} // namespace cirrus::netease
