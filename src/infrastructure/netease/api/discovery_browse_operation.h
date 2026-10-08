#pragma once
#include "api_operation.h"
#include "domain/music/discovery_browse_types.h"

namespace cirrus::netease
{
struct DiscoveryBrowseWire {
    ApiOperation operation;
    QJsonObject parameters;
};
[[nodiscard]] bool isDiscoveryBrowseOperation(ApiOperation operation);
[[nodiscard]] std::optional<ApiOperationContract> discoveryBrowseContract(ApiOperation operation);
[[nodiscard]] std::optional<QJsonObject> discoveryBrowsePayload(ApiOperation operation,
                                                                const QJsonObject& parameters);
[[nodiscard]] std::optional<DiscoveryBrowseWire>
discoveryBrowseRequest(const domain::DiscoveryBrowseQuery& query);
} // namespace cirrus::netease
