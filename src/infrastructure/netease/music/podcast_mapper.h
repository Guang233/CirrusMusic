#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"
#include <QJsonObject>
namespace cirrus::netease
{
struct PodcastRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<PodcastRequest> podcastRequest(const application::MusicQuery& query);
application::MusicResult mapPodcastResponse(const application::MusicQuery& query,
                                            const QJsonObject& object);
} // namespace cirrus::netease
