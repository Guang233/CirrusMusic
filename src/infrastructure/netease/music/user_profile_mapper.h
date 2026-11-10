#pragma once
#include "application/ports/music_gateway.h"
#include "infrastructure/netease/api/api_operation.h"
#include <QJsonObject>

namespace cirrus::netease
{
struct UserProfileWire {
    ApiOperation operation;
    QJsonObject parameters;
};
[[nodiscard]] std::optional<UserProfileWire>
userProfileRequest(const domain::UserProfileQuery& query);
// The provider's answer when the user hides a list (400) or the ranking (-2).
[[nodiscard]] bool userProfileHidden(const domain::UserProfileQuery& query, int providerCode);
[[nodiscard]] application::MusicResult mapUserProfileResponse(const domain::UserProfileQuery& query,
                                                              const QJsonObject& object);
} // namespace cirrus::netease
