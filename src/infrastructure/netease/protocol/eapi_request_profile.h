#pragma once

#include "protocol_crypto.h"

namespace cirrus::netease
{

// Reference pc profile, not hardware detection. The composition root must supply
// a stable device ID; this component does not generate/persist anonymous tokens.
struct EapiRequestMetadata {
    QString deviceId;
    qint64 timestampMs = 0;
    quint16 requestSuffix = 0; // 0000..0999 correlation suffix, NOT a crypto nonce
    QByteArray csrf;
    QByteArray musicU;
    QByteArray musicA;
    QByteArray nmtid;
};

struct PreparedEapiRequest {
    CryptoError error = CryptoError::None;
    QByteArray formBody;
    QByteArray cookieHeader;
    QByteArray userAgent;
    [[nodiscard]] bool ok() const { return error == CryptoError::None; }
};

// Explicit e_r=false / plaintext JSON response profile, as in pinned upstream.
// Cookie/JSON credentials remain infrastructure-only. No arbitrary UA, host,
// anti-cheat token, IP spoofing or response-encryption fallback is exposed.
[[nodiscard]] PreparedEapiRequest prepareEapiRequest(QStringView logicalPath, QJsonObject payload,
                                                     const EapiRequestMetadata& metadata);

} // namespace cirrus::netease
