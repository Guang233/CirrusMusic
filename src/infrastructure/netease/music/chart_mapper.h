#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"

namespace cirrus::netease
{
std::optional<ApiOperation> chartOperation(const application::MusicQuery& query);
application::MusicResult mapChartsResponse(const domain::ChartsQuery& query,
                                           const QJsonObject& object);
} // namespace cirrus::netease
