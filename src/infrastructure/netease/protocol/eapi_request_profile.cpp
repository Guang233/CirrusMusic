#include "eapi_request_profile.h"

#include <QUrl>

namespace cirrus::netease
{
namespace
{

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

} // namespace

PreparedEapiRequest prepareEapiRequest(QStringView logicalPath, QJsonObject payload,
                                       const EapiRequestMetadata& metadata)
{
    if (!printableAscii(metadata.deviceId, 128) || metadata.timestampMs <= 0 ||
        metadata.requestSuffix >= 1000 || !validCookieValue(metadata.csrf) ||
        !validCookieValue(metadata.musicU) || !validCookieValue(metadata.musicA) ||
        !validCookieValue(metadata.nmtid)) {
        return {CryptoError::InvalidInput};
    }
    QJsonObject header{
        {QStringLiteral("osver"),
         QStringLiteral("Microsoft-Windows-10-Professional-build-19045-64bit")},
        {QStringLiteral("deviceId"), metadata.deviceId},
        {QStringLiteral("os"), QStringLiteral("pc")},
        {QStringLiteral("appver"), QStringLiteral("3.1.17.204416")},
        {QStringLiteral("versioncode"), QStringLiteral("140")},
        {QStringLiteral("mobilename"), QString{}},
        {QStringLiteral("buildver"), QString::number(metadata.timestampMs / 1000)},
        {QStringLiteral("resolution"), QStringLiteral("1920x1080")},
        {QStringLiteral("__csrf"), QString::fromUtf8(metadata.csrf)},
        {QStringLiteral("channel"), QStringLiteral("netease")},
        {QStringLiteral("requestId"), QStringLiteral("%1_%2")
                                          .arg(metadata.timestampMs)
                                          .arg(metadata.requestSuffix, 4, 10, QLatin1Char('0'))}};
    const auto insertCookie = [&header](const QString& name, const QByteArray& value) {
        if (!value.isEmpty()) {
            header.insert(name, QString::fromUtf8(value));
        }
    };
    insertCookie(QStringLiteral("MUSIC_U"), metadata.musicU);
    insertCookie(QStringLiteral("MUSIC_A"), metadata.musicA);
    insertCookie(QStringLiteral("NMTID"), metadata.nmtid);

    PreparedEapiRequest result;
    for (auto it = header.constBegin(); it != header.constEnd(); ++it) {
        if (!result.cookieHeader.isEmpty()) {
            result.cookieHeader.append("; ");
        }
        // JS encodeURIComponent, not form encoding: spaces are %20, never +.
        result.cookieHeader.append(QUrl::toPercentEncoding(it.key(), "!'()*"));
        result.cookieHeader.append('=');
        result.cookieHeader.append(QUrl::toPercentEncoding(it.value().toString(), "!'()*"));
    }
    if (result.cookieHeader.size() > 16 * 1024) {
        return {CryptoError::InvalidInput};
    }
    payload.insert(QStringLiteral("e_r"), false);
    payload.insert(QStringLiteral("header"), header);
    const auto encrypted = encodeEapi(logicalPath, payload);
    if (!encrypted.ok()) {
        return {encrypted.error};
    }
    result.formBody = encodeForm(encrypted.fields);
    // Deliberately preserve the pinned reference's pc + api/iphone UA choice;
    // this is a compatibility profile, not a claim about the user's machine.
    result.userAgent = "NeteaseMusic 9.0.90/5038 (iPhone; iOS 16.2; zh_CN)";
    return result;
}

} // namespace cirrus::netease
