#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"

namespace cirrus::netease
{
struct CloudHistoryRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<CloudHistoryRequest> cloudHistoryRequest(const application::MusicQuery& query);
application::MusicResult mapCloudHistoryResponse(const application::MusicQuery& query,
                                                 const QJsonObject& object);
} // namespace cirrus::netease
