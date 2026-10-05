#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"
namespace cirrus::netease
{
struct EntitySearchRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
[[nodiscard]] std::optional<EntitySearchRequest>
entitySearchRequest(const application::MusicQuery& query);
[[nodiscard]] application::MusicResult mapEntitySearchResponse(const application::MusicQuery& query,
                                                               const QJsonObject& object);
} // namespace cirrus::netease
