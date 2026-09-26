#pragma once

#include <QByteArray>
#include <QJsonObject>

#include <optional>

namespace cirrus::netease
{

enum class ResponseEncoding { Json, GzipJson, EapiJson, EapiGzipJson, EapiAutoJson };
enum class DecodeError {
    None,
    InvalidLimits,
    InputTooLarge,
    DecryptionFailed,
    InvalidGzip,
    DecodedTooLarge,
    InvalidUtf8,
    InvalidJson,
    UnexpectedRoot,
    InvalidBusinessCode,
    UnsupportedEncoding
};

struct DecodeLimits {
    qsizetype inputBytes = 1024 * 1024 + 16;
    qsizetype jsonBytes = 4 * 1024 * 1024;
};

struct DecodedResponse {
    DecodeError error = DecodeError::None;
    QJsonObject object;
    std::optional<int> businessCode;
    // Only indicates valid decoding/envelope shape, NOT business success.
    [[nodiscard]] bool ok() const { return error == DecodeError::None; }
};

// Pure bounded decoding: no HTTP, cookies, logging, retries or domain mapping.
// Gzip accepts one complete member only, with no trailing bytes; verifies CRC.
// Encoding is chosen by the operation contract, never guessed after a failure.
[[nodiscard]] DecodedResponse decodeResponse(const QByteArray& body, ResponseEncoding encoding,
                                             DecodeLimits limits = {});

} // namespace cirrus::netease
