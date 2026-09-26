#include "response_decoder.h"

#include "protocol_crypto.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringView>

#include <zlib.h>

#include <array>
#include <cmath>
#include <limits>

namespace cirrus::netease
{
namespace
{

constexpr qsizetype HardLimit = 32 * 1024 * 1024;

struct Inflater {
    z_stream stream{};
    bool initialized = false;
    ~Inflater()
    {
        if (initialized) {
            inflateEnd(&stream);
        }
    }
};

struct ExpandedBody {
    DecodeError error = DecodeError::None;
    QByteArray bytes;
};

ExpandedBody gunzip(const QByteArray& compressed, qsizetype maximum)
{
    Inflater inflater;
    if (inflateInit2(&inflater.stream, MAX_WBITS + 16) != Z_OK) {
        return {DecodeError::InvalidGzip, {}};
    }
    inflater.initialized = true;
    auto& stream = inflater.stream;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    QByteArray output;
    std::array<char, 16384> buffer{};
    for (;;) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        const auto previousInput = stream.avail_in;
        const int status = inflate(&stream, Z_NO_FLUSH);
        const auto count = static_cast<qsizetype>(buffer.size() - stream.avail_out);
        if (count > maximum - output.size()) {
            return {DecodeError::DecodedTooLarge, {}};
        }
        output.append(buffer.data(), count);
        if (status == Z_STREAM_END) {
            if (stream.avail_in != 0) {
                return {DecodeError::InvalidGzip, {}};
            }
            return {DecodeError::None, output};
        }
        if (status != Z_OK || (stream.avail_in == previousInput && count == 0)) {
            return {DecodeError::InvalidGzip, {}};
        }
    }
}

std::optional<int> parseBusinessCode(const QJsonValue& value)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number) && std::trunc(number) == number &&
            number >= std::numeric_limits<int>::min() &&
            number <= std::numeric_limits<int>::max()) {
            return static_cast<int>(number);
        }
        return std::nullopt;
    }
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto text = value.toString();
    QStringView digits(text);
    if (digits.startsWith(u'-')) {
        digits = digits.sliced(1);
    }
    if (digits.isEmpty()) {
        return std::nullopt;
    }
    for (QChar character : digits) {
        if (character < u'0' || character > u'9') {
            return std::nullopt;
        }
    }
    bool ok = false;
    const int code = text.toInt(&ok);
    return ok ? std::optional<int>(code) : std::nullopt;
}

} // namespace

DecodedResponse decodeResponse(const QByteArray& body, ResponseEncoding encoding,
                               DecodeLimits limits)
{
    if (limits.inputBytes <= 0 || limits.jsonBytes <= 0 || limits.inputBytes > HardLimit ||
        limits.jsonBytes > HardLimit) {
        return {DecodeError::InvalidLimits, {}, {}};
    }
    if (body.size() > limits.inputBytes) {
        return {DecodeError::InputTooLarge, {}, {}};
    }
    QByteArray plaintext;
    bool compressed = false;
    switch (encoding) {
    case ResponseEncoding::Json:
    case ResponseEncoding::GzipJson:
        plaintext = body;
        compressed = encoding == ResponseEncoding::GzipJson;
        break;
    case ResponseEncoding::EapiJson:
    case ResponseEncoding::EapiGzipJson:
    case ResponseEncoding::EapiAutoJson: {
        if (body.size() > MaxProtocolPlaintext + 16) {
            return {DecodeError::InputTooLarge, {}, {}};
        }
        const auto decrypted = decryptEapi(body);
        if (!decrypted.ok()) {
            return {DecodeError::DecryptionFailed, {}, {}};
        }
        plaintext = decrypted.bytes;
        compressed =
            encoding == ResponseEncoding::EapiGzipJson ||
            (encoding == ResponseEncoding::EapiAutoJson && plaintext.startsWith("\x1f\x8b"));
        break;
    }
    default:
        return {DecodeError::UnsupportedEncoding, {}, {}};
    }
    if (compressed) {
        const auto expanded = gunzip(plaintext, limits.jsonBytes);
        if (expanded.error != DecodeError::None) {
            return {expanded.error, {}, {}};
        }
        plaintext = expanded.bytes;
    }
    if (plaintext.size() > limits.jsonBytes) {
        return {DecodeError::DecodedTooLarge, {}, {}};
    }
    if (!plaintext.isValidUtf8()) {
        return {DecodeError::InvalidUtf8, {}, {}};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(plaintext, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {DecodeError::InvalidJson, {}, {}};
    }
    if (!document.isObject()) {
        return {DecodeError::UnexpectedRoot, {}, {}};
    }
    const auto object = document.object();
    std::optional<int> code;
    if (object.contains(QStringLiteral("code"))) {
        code = parseBusinessCode(object.value(QStringLiteral("code")));
        if (!code) {
            return {DecodeError::InvalidBusinessCode, {}, {}};
        }
    }
    return {DecodeError::None, object, code};
}

} // namespace cirrus::netease
