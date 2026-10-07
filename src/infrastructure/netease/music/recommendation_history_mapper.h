#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"

namespace cirrus::netease
{
struct RecommendationHistoryRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<RecommendationHistoryRequest>
recommendationHistoryRequest(const application::MusicQuery& query);
application::MusicResult mapRecommendationHistoryResponse(const application::MusicQuery& query,
                                                          const QJsonObject& object);
} // namespace cirrus::netease
