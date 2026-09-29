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
    case ApiOperation::AccountIdentity:
        return ApiOperationContract{QStringLiteral("/api/w/nuser/account/get"), Protocol::Weapi};
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
    if (operation == ApiOperation::AccountIdentity) {
        return parameters.isEmpty() ? std::optional<QJsonObject>(QJsonObject{}) : std::nullopt;
    }
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
    case ApiOperation::AccountIdentity:
        break;
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
    case ApiOperation::AccountIdentity:
        return *response.businessCode == 200; // staged only in its isolated request snapshot
    }
    return false;
}

} // namespace cirrus::netease
