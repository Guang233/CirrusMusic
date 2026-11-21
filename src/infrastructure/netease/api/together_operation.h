#pragma once
#include "api_operation.h"
#include "domain/music/together_types.h"
namespace cirrus::netease
{
[[nodiscard]] bool isTogetherOperation(ApiOperation operation);
[[nodiscard]] std::optional<ApiOperationContract> togetherContract(ApiOperation operation);
// The parameters must be exactly what togetherParameters makes for some valid action.
[[nodiscard]] std::optional<QJsonObject> togetherPayload(ApiOperation operation,
                                                         const QJsonObject& parameters);
[[nodiscard]] ApiOperation togetherOperation(domain::TogetherKind kind);
// The wire as the official Android app sends it: every top-level value a string, commands
// and versions as JSON text (docs/api/together-polling.md). Refer values are added by
// togetherPayload.
[[nodiscard]] QJsonObject togetherParameters(const domain::TogetherAction& action);

// Shared by the response mapping.
[[nodiscard]] std::optional<qint64> togetherCounter(const QJsonValue& value);
[[nodiscard]] std::optional<QString> togetherUser(const QJsonValue& value);
[[nodiscard]] std::optional<domain::TogetherPlayAction> togetherPlayAction(const QJsonValue& v);
[[nodiscard]] std::optional<domain::TogetherPlayStatus> togetherPlayStatus(const QJsonValue& v);
[[nodiscard]] std::optional<domain::TogetherPlayMode> togetherPlayMode(const QJsonValue& value);
[[nodiscard]] std::optional<domain::TogetherTrigger> togetherTrigger(const QJsonValue& value);
[[nodiscard]] std::optional<QList<domain::TogetherVersion>>
togetherVersions(const QJsonValue& value);
} // namespace cirrus::netease
