#pragma once
#include "api_operation.h"
namespace cirrus::netease
{
[[nodiscard]] bool isCommentWriteOperation(ApiOperation op);
[[nodiscard]] bool requiresCommentV3(ApiOperation op);
[[nodiscard]] std::optional<ApiOperationContract> commentWriteContract(ApiOperation op);
[[nodiscard]] std::optional<QJsonObject> commentWritePayload(ApiOperation op,
                                                             const QJsonObject& parameters);
} // namespace cirrus::netease
