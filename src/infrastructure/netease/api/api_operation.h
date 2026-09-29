#pragma once

#include "infrastructure/netease/protocol/official_route.h"
#include "infrastructure/netease/protocol/response_decoder.h"

namespace cirrus::netease
{

// Closed native registry. No string-based forwarding from application/QML.
enum class ApiOperation { QrKey, QrCheck };

struct ApiOperationContract {
    QString logicalPath;
    Protocol protocol = Protocol::Eapi;
    ResponseEncoding responseEncoding = ResponseEncoding::Json;
    qsizetype responseLimit = 64 * 1024;
};

[[nodiscard]] std::optional<ApiOperationContract> operationContract(ApiOperation operation);
[[nodiscard]] bool validQrKey(QStringView key);
[[nodiscard]] std::optional<QJsonObject> operationPayload(ApiOperation operation,
                                                          const QJsonObject& parameters);
// Only recognized operation outcomes may stage response cookies. Not proof of
// account identity; in particular QR 803 still requires identity verification.
[[nodiscard]] bool acceptsResponseCookies(ApiOperation operation, const DecodedResponse& response);

} // namespace cirrus::netease
