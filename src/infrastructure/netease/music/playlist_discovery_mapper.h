#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"

namespace cirrus::netease
{
struct PlaylistDiscoveryRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<PlaylistDiscoveryRequest>
playlistDiscoveryRequest(const application::MusicQuery& query);
application::MusicResult mapPlaylistDiscoveryResponse(const application::MusicQuery& query,
                                                      const QJsonObject& object);
} // namespace cirrus::netease
