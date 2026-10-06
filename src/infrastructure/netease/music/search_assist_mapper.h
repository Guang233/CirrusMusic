#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"
namespace cirrus::netease
{
struct SearchAssistRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<SearchAssistRequest> searchAssistRequest(const application::MusicQuery& query);
application::MusicResult mapSearchAssistResponse(const application::MusicQuery& query,
                                                 const QJsonObject& object);
} // namespace cirrus::netease
