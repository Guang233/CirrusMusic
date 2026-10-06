#pragma once
#include "official_route.h"
namespace cirrus::netease
{
inline bool validCommentV3Token(const QByteArray& token)
{
    if (token.isEmpty() || token.size() > 4096)
        return false;
    for (const unsigned char c : token)
        if (c < 0x21 || c > 0x7e)
            return false;
    return true;
}
inline bool commentV3Path(Protocol protocol, const QString& path)
{
    return protocol == Protocol::Xeapi &&
           (path == QStringLiteral("/api/resource/comments/add") ||
            path == QStringLiteral("/api/v1/resource/comments/reply"));
}
} // namespace cirrus::netease
