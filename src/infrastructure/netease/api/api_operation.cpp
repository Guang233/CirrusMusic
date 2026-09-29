#include "api_operation.h"

namespace cirrus::netease
{

std::optional<ApiOperationContract> operationContract(ApiOperation operation)
{
    switch (operation) {
    case ApiOperation::QrKey:
        return ApiOperationContract{QStringLiteral("/api/login/qrcode/unikey")};
    case ApiOperation::QrCheck:
        return ApiOperationContract{QStringLiteral("/api/login/qrcode/client/login")};
    }
    return std::nullopt;
}

bool validQrKey(QStringView key)
{
    if (key.isEmpty() || key.size() > 512) {
        return false;
    }
    for (const auto c : key) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e) {
            return false;
        }
    }
    return true;
}

std::optional<QJsonObject> operationPayload(ApiOperation operation, const QJsonObject& parameters)
{
    QJsonObject payload{{QStringLiteral("type"), 3}};
    switch (operation) {
    case ApiOperation::QrKey:
        if (parameters.isEmpty()) {
            return payload;
        }
        break;
    case ApiOperation::QrCheck: {
        const auto key = parameters.value(QStringLiteral("key"));
        if (parameters.size() == 1 && key.isString() && validQrKey(key.toString())) {
            payload.insert(QStringLiteral("key"), key);
            return payload;
        }
        break;
    }
    }
    return std::nullopt;
}

bool acceptsResponseCookies(ApiOperation operation, const DecodedResponse& response)
{
    if (!response.ok() || !response.businessCode) {
        return false;
    }
    switch (operation) {
    case ApiOperation::QrKey:
        return *response.businessCode == 200 &&
               validQrKey(response.object.value(QStringLiteral("unikey")).toString());
    case ApiOperation::QrCheck:
        return *response.businessCode >= 801 && *response.businessCode <= 803;
    }
    return false;
}

} // namespace cirrus::netease
