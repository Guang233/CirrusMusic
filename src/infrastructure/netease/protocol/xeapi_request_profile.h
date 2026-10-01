#pragma once

#include "xeapi_crypto.h"

#include <utility>

namespace cirrus::netease
{

// Compatibility subset of the pinned reference's android xeapi profile. Not
// hardware detection; tracking cookies (_ntes_nuid, WNMCID...) are not sent.
struct XeapiRequestMetadata {
    QString deviceId;
    qint64 timestampMs = 0;
    QByteArray csrf;
    QByteArray musicU;
    QByteArray musicA; // only sent when there is no MUSIC_U, as in the reference
    QByteArray nmtid;
};

using HeaderList = QList<std::pair<QByteArray, QByteArray>>;

struct PreparedXeapiRequest {
    CryptoError error = CryptoError::None;
    QByteArray formBody;
    QByteArray cookieHeader;
    QByteArray userAgent;
    HeaderList headers; // fixed x-* profile headers; transport re-validates names
    [[nodiscard]] bool ok() const { return error == CryptoError::None; }
};

// JSON scalars only (string, bool, safe integer); nested values are rejected.
[[nodiscard]] std::optional<QList<FormField>> xeapiFormFields(const QJsonObject& payload);

[[nodiscard]] PreparedXeapiRequest
prepareXeapiRequest(const QList<FormField>& body, const XeapiPublicKey& key,
                    const std::optional<XeapiSessionKey>& session,
                    const XeapiRequestMetadata& metadata,
                    const RandomBytes& random = secureRandomBytes);

// Names the transport accepts for Protocol::Xeapi; any other header is refused.
[[nodiscard]] bool isXeapiProfileHeader(QByteArrayView name);

} // namespace cirrus::netease
