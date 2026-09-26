#pragma once

#include "form_encoding.h"

#include <QJsonObject>

#include <functional>
#include <optional>

namespace cirrus::netease
{

enum class CryptoError { None, InvalidInput, RandomFailure, EncryptionFailure, DecryptionFailure };

struct EncodedPayload {
    CryptoError error = CryptoError::None;
    QList<FormField> fields;
    [[nodiscard]] bool ok() const { return error == CryptoError::None; }
};

struct DecryptedPayload {
    CryptoError error = CryptoError::None;
    QByteArray bytes;
    [[nodiscard]] bool ok() const { return error == CryptoError::None; }
};

inline constexpr qsizetype MaxProtocolPlaintext = 1024 * 1024;

// Injection seam for offline tests, never a user-configurable seed/key.
using RandomBytes = std::function<std::optional<QByteArray>(qsizetype)>;
[[nodiscard]] std::optional<QByteArray> secureRandomBytes(qsizetype size);
[[nodiscard]] EncodedPayload encodeWeapi(const QJsonObject& payload,
                                         const RandomBytes& random = secureRandomBytes);
[[nodiscard]] EncodedPayload encodeEapi(QStringView logicalPath, const QJsonObject& payload);

// Binary AES-ECB ciphertext, not hex text. Only decrypts PKCS#7; the caller must
// separately validate JSON (or decompress explicitly). No success fallback.
[[nodiscard]] DecryptedPayload decryptEapi(const QByteArray& ciphertext);

} // namespace cirrus::netease
