#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"
#include <QJsonObject>
namespace cirrus::netease
{
struct RecentEntityRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<RecentEntityRequest> recentEntityRequest(const application::MusicQuery& query);
application::MusicResult mapRecentEntityResponse(const application::MusicQuery& query,
                                                 const QJsonObject& object);
} // namespace cirrus::netease
