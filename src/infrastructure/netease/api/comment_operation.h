#pragma once
#include "api_operation.h"
#include "domain/music/comment_types.h"
namespace cirrus::netease
{
bool isCommentOperation(ApiOperation operation);
QString commentKindName(const domain::CommentTarget& target);
QString commentThreadId(const domain::CommentTarget& target);
int commentResourceType(const domain::CommentTarget& target);
std::optional<ApiOperationContract> commentOperationContract(ApiOperation operation,
                                                             const QJsonObject& parameters);
std::optional<QJsonObject> commentOperationPayload(ApiOperation operation,
                                                   const QJsonObject& parameters);
} // namespace cirrus::netease
