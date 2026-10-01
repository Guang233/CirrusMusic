#pragma once

#include "protocol_crypto.h"

namespace cirrus::netease
{

// Server-issued installation key state. version/publicKey are public; sk is secret.
struct XeapiPublicKey {
    QString version;      // printable ASCII, <= 64
    QByteArray publicKey; // raw X25519, exactly 32 bytes
    QByteArray sk;        // non-empty UTF-8 without control characters, <= 1024
    [[nodiscard]] bool isValid() const;
};

// From x-encr-ssid / x-encr-sskey. Secret; bound to one session epoch by the caller.
struct XeapiSessionKey {
    QByteArray id;  // printable ASCII, <= 256
    QByteArray key; // printable ASCII of exactly 16, 24 or 32 bytes (used as AES key)
    [[nodiscard]] bool isValid() const;
};

// Closed request subset of the pinned reference: POST, form body, no URL query,
// encrypted response. Fields named e_r are dropped. Random draws, in order:
// dynamic key (16, only without session), mid mask (16), ephemeral X25519
// private key (32), GCM IV (12). Returns fields B, S, R.
[[nodiscard]] EncodedPayload encodeXeapi(const QList<FormField>& body, const XeapiPublicKey& key,
                                         const std::optional<XeapiSessionKey>& session = {},
                                         const RandomBytes& random = secureRandomBytes);

// Base64 HMAC-SHA256 used by key registration (request and response signatures).
[[nodiscard]] QByteArray xeapiSignature(QByteArrayView timestamp, QByteArrayView nonce);

// Decrypts and validates the registration response's encryptedData (Base64).
[[nodiscard]] std::optional<XeapiPublicKey> decryptXeapiPublicKey(QByteArrayView encryptedData);

} // namespace cirrus::netease
