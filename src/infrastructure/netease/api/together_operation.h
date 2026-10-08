#pragma once
#include "api_operation.h"
namespace cirrus::netease
{
[[nodiscard]] bool isTogetherOperation(ApiOperation operation);
[[nodiscard]] std::optional<ApiOperationContract> togetherContract(ApiOperation operation);
[[nodiscard]] std::optional<QJsonObject> togetherPayload(ApiOperation operation,
                                                         const QJsonObject& parameters);
} // namespace cirrus::netease
