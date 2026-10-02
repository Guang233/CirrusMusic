#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"

namespace cirrus::netease
{
struct CatalogRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<CatalogRequest> catalogRequest(const application::MusicQuery& query);
application::MusicResult mapCatalogResponse(const application::MusicQuery& query,
                                            const QJsonObject& object);
} // namespace cirrus::netease
