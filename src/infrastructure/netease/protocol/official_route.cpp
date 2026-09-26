#include "official_route.h"

#include <QString>

namespace cirrus::netease
{
namespace
{

bool isCanonicalPath(QStringView path)
{
    if (!path.startsWith(u"/api/") || path.size() <= 5 || path.endsWith(u'/')) {
        return false;
    }
    bool previousSlash = false;
    for (QChar character : path) {
        const auto code = character.unicode();
        if (code == '/') {
            if (previousSlash) {
                return false;
            }
            previousSlash = true;
            continue;
        }
        previousSlash = false;
        if (!((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') ||
              (code >= '0' && code <= '9') || code == '_' || code == '-')) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<QUrl> officialRequestUrl(Protocol protocol, QStringView logicalPath)
{
    if (!isCanonicalPath(logicalPath)) {
        return std::nullopt;
    }

    using namespace Qt::StringLiterals;
    QString host;
    QString prefix;
    switch (protocol) {
    case Protocol::Api:
        host = u"interface.music.163.com"_s;
        prefix = u"/api/"_s;
        break;
    case Protocol::Weapi:
        host = u"music.163.com"_s;
        prefix = u"/weapi/"_s;
        break;
    case Protocol::Eapi:
        host = u"interfacepc.music.163.com"_s;
        prefix = u"/eapi/"_s;
        break;
    case Protocol::Xeapi:
        host = u"interface3.music.163.com"_s;
        prefix = u"/xeapi/"_s;
        break;
    default:
        return std::nullopt;
    }

    QUrl url;
    url.setScheme(u"https"_s);
    url.setHost(host);
    url.setPath(prefix + logicalPath.sliced(5).toString());
    return url;
}

} // namespace cirrus::netease
