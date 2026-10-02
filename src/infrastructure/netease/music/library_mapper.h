#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"

namespace cirrus::netease
{
struct LibraryRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<LibraryRequest> libraryRequest(const application::MusicQuery& query);
application::MusicResult mapLibraryResponse(const application::MusicQuery& query,
                                            const QJsonObject& object);
} // namespace cirrus::netease
