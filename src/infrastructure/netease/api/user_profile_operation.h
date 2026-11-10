#pragma once
#include "api_operation.h"

namespace cirrus::netease
{
// Another user's public profile (docs/ui/pages/user.md §1): details, playlists, follows,
// fans and listening ranking. Reads only; none needs a signed-in account.
[[nodiscard]] bool isUserProfileOperation(ApiOperation operation);
[[nodiscard]] std::optional<ApiOperationContract>
userProfileContract(ApiOperation operation, const QJsonObject& parameters);
[[nodiscard]] std::optional<QJsonObject> userProfilePayload(ApiOperation operation,
                                                            const QJsonObject& parameters);
} // namespace cirrus::netease
