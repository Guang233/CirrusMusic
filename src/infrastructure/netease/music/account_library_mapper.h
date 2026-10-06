#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"
namespace cirrus::netease
{
struct AccountLibraryRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<AccountLibraryRequest> accountLibraryRequest(const application::MusicQuery& query);
application::MusicResult mapAccountLibraryResponse(const application::MusicQuery& query,
                                                   const QJsonObject& object);
} // namespace cirrus::netease
