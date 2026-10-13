#pragma once
#include "domain/music/cache_retention.h"
#include <QDateTime>
#include <QNetworkReply>
#include <algorithm>
#include <optional>

namespace cirrus::network
{
// Conservative application-data retention, NOT a general HTTP/POST response cache.
inline domain::CacheRetention cacheRetention(const QNetworkReply& reply, qint64 elapsedMs)
{
    constexpr qint64 Cap = 7LL * 86400000;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now <= 0 || now > 253402300799999LL - Cap || elapsedMs < 0 || elapsedMs >= Cap ||
        reply.error() != QNetworkReply::NoError ||
        reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
        return {};
    const auto headers = reply.rawHeaderPairs();
    if (headers.size() > 64)
        return {};
    qsizetype bytes = 0;
    QList<QByteArray> policyHeaders;
    for (const auto& h : headers) {
        bytes += h.first.size() + h.second.size();
        if (bytes > 16384)
            return {};
        const auto name = h.first.toLower();
        if (name == "cache-control" || name == "date" || name == "age" || name == "expires") {
            if (policyHeaders.contains(name))
                return {}; // do not depend on duplicate-field merging
            policyHeaders.append(name);
        }
    }
    if (reply.hasRawHeader("Vary") || reply.hasRawHeader("Pragma") ||
        reply.hasRawHeader("Set-Cookie"))
        return {};
    const auto seconds = [](const QByteArray& text) -> std::optional<qint64> {
        if (text.isEmpty() || text.size() > 9 ||
            !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        bool ok = false;
        const auto value = text.toLongLong(&ok);
        return ok ? std::optional<qint64>(value * 1000) : std::nullopt;
    };
    const auto date = [](const QByteArray& value) -> std::optional<qint64> {
        if (value.isEmpty() || value.size() > 128)
            return std::nullopt;
        const auto parsed = QDateTime::fromString(QString::fromLatin1(value), Qt::RFC2822Date);
        if (!parsed.isValid() || parsed.toMSecsSinceEpoch() <= 0 ||
            parsed.toMSecsSinceEpoch() > 253402300799999LL)
            return std::nullopt;
        return parsed.toMSecsSinceEpoch();
    };
    const auto servedAt = date(reply.rawHeader("Date"));
    if (!servedAt || *servedAt > now)
        return {};
    std::optional<qint64> lifetime;
    QList<QByteArray> seen;
    const auto control = reply.rawHeader("Cache-Control").toLower();
    if (control.size() > 4096)
        return {};
    if (!control.isEmpty())
        for (const auto& raw : control.split(',')) {
            const auto token = raw.trimmed();
            const auto name = token.split('=').value(0);
            if (token.isEmpty() || seen.contains(name))
                return {};
            seen.append(name);
            if (token.startsWith("max-age=")) {
                lifetime = seconds(token.mid(8));
                if (!lifetime || *lifetime <= 0)
                    return {};
            } else if (token != "public" && token != "private" && token != "must-revalidate" &&
                       token != "immutable")
                return {}; // includes no-store, no-cache, extension/qualified directives
        }
    if (seen.contains("public") && seen.contains("private"))
        return {};
    if (reply.hasRawHeader("Expires")) {
        const auto expires = date(reply.rawHeader("Expires"));
        if (!expires || *expires <= *servedAt)
            return {};
        lifetime = lifetime ? std::min(*lifetime, *expires - *servedAt) : *expires - *servedAt;
    }
    if (!lifetime)
        return {}; // no heuristic freshness
    qint64 age = now - *servedAt;
    if (reply.hasRawHeader("Age")) {
        const auto declaredAge = seconds(reply.rawHeader("Age").trimmed());
        if (!declaredAge)
            return {};
        age = std::max(age, *declaredAge);
    }
    const auto remaining = std::max<qint64>(0, std::min(Cap, *lifetime) - age - elapsedMs);
    return {QDeadlineTimer(remaining, Qt::PreciseTimer)};
}
} // namespace cirrus::network
