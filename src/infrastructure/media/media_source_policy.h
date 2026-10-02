#pragma once
#include <QUrl>
#include <optional>

namespace cirrus::media
{
// Closed CDN scope, NOT arbitrary forwarding. Only provider-issued initial HTTP
// URLs may be upgraded; redirects must already be HTTPS (no downgrade).
inline std::optional<QUrl> approvedMediaUrl(QUrl url, bool upgradeInitialHttp = false)
{
    const auto host = url.host();
    if (!url.isValid() || url.toEncoded().size() > 8192 || url.isRelative() ||
        !url.userInfo().isEmpty() || url.hasFragment() || url.port(-1) != -1 ||
        !host.endsWith(QStringLiteral(".music.126.net")) || host.size() > 253 ||
        url.path().isEmpty() || url.path() == QStringLiteral("/"))
        return std::nullopt;
    for (const auto c : host) {
        if (!(c >= u'a' && c <= u'z') && !(c >= u'0' && c <= u'9') && c != u'-' && c != u'.')
            return std::nullopt;
    }
    if (url.scheme() == QStringLiteral("http") && upgradeInitialHttp)
        url.setScheme(QStringLiteral("https"));
    if (url.scheme() != QStringLiteral("https"))
        return std::nullopt;
    return url;
}
} // namespace cirrus::media
