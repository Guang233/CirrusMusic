// Protocol adaptation based on api-enhanced util/request.js (MIT).
// See third_party/api-enhanced/NOTICE.md and LICENSE for attribution.
#include "xeapi_request_profile.h"

#include <QJsonValue>
#include <QUrl>

namespace cirrus::netease
{
namespace
{

constexpr char Os[] = "android";
constexpr char OsVersion[] = "16";
constexpr char AppVersion[] = "9.1.65";
constexpr char UserAgent[] = "NeteaseMusic/9.5.61.260802021928(9005061);Dalvik/2.1.0 (Linux; U; "
                             "Android 12; HBN-AL00 Build/cd737a2.0)";
constexpr const char* ProfileHeaders[] = {
    "X-Client-Enc-State", "x-aeapi",     "x-deviceid", "x-os",     "x-osver",
    "x-appver",           "x-sdeviceid", "x-buildver", "x-music-u"};

bool printableAscii(QStringView value, qsizetype limit)
{
    if (value.isEmpty() || value.size() > limit) {
        return false;
    }
    for (const auto c : value) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e) {
            return false;
        }
    }
    return true;
}

bool validCookieValue(const QByteArray& value)
{
    if (value.size() > 4096 || !value.isValidUtf8()) {
        return false;
    }
    for (const unsigned char c : value) {
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

QByteArray uriComponent(const QByteArray& value)
{
    return QUrl::toPercentEncoding(QString::fromUtf8(value), "!'()*"); // encodeURIComponent
}

} // namespace

std::optional<QList<FormField>> xeapiFormFields(const QJsonObject& payload)
{
    QList<FormField> fields;
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        const auto value = it.value();
        QString text;
        if (value.isString()) {
            text = value.toString();
        } else if (value.isBool()) {
            text = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (value.isDouble() && qAbs(value.toDouble()) <= 9007199254740991.0 &&
                   value.toDouble() == static_cast<double>(value.toInteger())) {
            text = QString::number(value.toInteger());
        } else {
            return std::nullopt;
        }
        fields.append({it.key(), text});
    }
    return fields;
}

bool isXeapiProfileHeader(QByteArrayView name)
{
    for (const char* allowed : ProfileHeaders) {
        if (name == QByteArrayView(allowed)) {
            return true;
        }
    }
    return false;
}

PreparedXeapiRequest prepareXeapiRequest(const QList<FormField>& body, const XeapiPublicKey& key,
                                         const std::optional<XeapiSessionKey>& session,
                                         const XeapiRequestMetadata& metadata,
                                         const RandomBytes& random)
{
    if (!printableAscii(metadata.deviceId, 128) || metadata.timestampMs <= 0 ||
        !validCookieValue(metadata.csrf) || !validCookieValue(metadata.musicU) ||
        !validCookieValue(metadata.musicA) || !validCookieValue(metadata.nmtid)) {
        return {CryptoError::InvalidInput};
    }
    const auto encrypted = encodeXeapi(body, key, session, random);
    if (!encrypted.ok()) {
        return {encrypted.error};
    }
    const auto deviceId = metadata.deviceId.toLatin1();
    const auto buildver = QByteArray::number(metadata.timestampMs / 1000);

    PreparedXeapiRequest result;
    result.formBody = encodeForm(encrypted.fields);
    result.userAgent = UserAgent;
    result.headers = {{"X-Client-Enc-State", "ENCRYPTED"},
                      {"x-aeapi", "true"},
                      {"x-deviceid", deviceId},
                      {"x-os", Os},
                      {"x-osver", OsVersion},
                      {"x-appver", AppVersion},
                      {"x-sdeviceid", deviceId},
                      {"x-buildver", buildver}};
    if (!metadata.musicU.isEmpty()) {
        result.headers.append({"x-music-u", metadata.musicU});
    }
    HeaderList cookies;
    if (!metadata.musicU.isEmpty()) {
        cookies.append({"MUSIC_U", metadata.musicU});
    } else if (!metadata.musicA.isEmpty()) {
        cookies.append({"MUSIC_A", metadata.musicA});
    }
    if (!metadata.csrf.isEmpty()) {
        cookies.append({"__csrf", metadata.csrf});
    }
    if (!metadata.nmtid.isEmpty()) {
        cookies.append({"NMTID", metadata.nmtid});
    }
    cookies.append({{"os", Os},
                    {"osver", OsVersion},
                    {"appver", AppVersion},
                    {"buildver", buildver},
                    {"channel", "netease"},
                    {"deviceId", deviceId},
                    {"sDeviceId", deviceId}});
    for (const auto& [name, value] : cookies) {
        if (!result.cookieHeader.isEmpty()) {
            result.cookieHeader.append("; ");
        }
        result.cookieHeader.append(name + '=' + uriComponent(value));
    }
    if (result.cookieHeader.size() > 16 * 1024) {
        return {CryptoError::InvalidInput};
    }
    return result;
}

} // namespace cirrus::netease
