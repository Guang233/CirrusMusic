#pragma once

#include "infrastructure/netease/protocol/protocol_crypto.h"

#include <QJsonDocument>
#include <QUrlQuery>

namespace cirrus::test
{

struct WireEapiRequest {
    QString logicalPath;
    QJsonObject payload;
};

// Test inspection only, using synthetic requests. Primitive interoperability is
// independently covered by the fixed OpenSSL/raw-RSA vectors in crypto tests.
inline std::optional<WireEapiRequest> decodeEapiRequest(const QByteArray& formBody)
{
    const QUrlQuery form(QString::fromLatin1(formBody));
    if (form.queryItems().size() != 1 || !form.hasQueryItem(QStringLiteral("params"))) {
        return std::nullopt;
    }
    const auto cipher = QByteArray::fromHex(
        form.queryItemValue(QStringLiteral("params"), QUrl::FullyDecoded).toLatin1());
    const auto plain = netease::decryptEapi(cipher);
    const QByteArray separator = "-36cd479b6b5-";
    const auto first = plain.bytes.indexOf(separator);
    const auto last = plain.bytes.lastIndexOf(separator);
    if (!plain.ok() || first <= 0 || last <= first) {
        return std::nullopt;
    }
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(
        plain.bytes.mid(first + separator.size(), last - first - separator.size()), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) {
        return std::nullopt;
    }
    return WireEapiRequest{QString::fromUtf8(plain.bytes.first(first)), json.object()};
}

} // namespace cirrus::test
