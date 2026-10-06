#pragma once
#include "api_operation.h"
namespace cirrus::netease
{
[[nodiscard]] bool isCollectionOperation(ApiOperation operation);
[[nodiscard]] std::optional<ApiOperationContract>
collectionOperationContract(ApiOperation operation);
[[nodiscard]] std::optional<QJsonObject> collectionOperationPayload(ApiOperation operation,
                                                                    const QJsonObject& parameters);
} // namespace cirrus::netease
