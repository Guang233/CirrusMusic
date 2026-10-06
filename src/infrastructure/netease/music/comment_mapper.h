#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"
namespace cirrus::netease
{
struct CommentRequest {
    ApiOperation operation;
    QJsonObject parameters;
};
std::optional<CommentRequest> commentRequest(const application::MusicQuery& query);
application::MusicResult mapCommentResponse(const application::MusicQuery& query,
                                            const QJsonObject& object);
} // namespace cirrus::netease
