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

std::optional<QUrl> officialRequestUrl(Protocol protocol, QStringView logicalPath,
                                       OfficialDestination destination)
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

    if (destination == OfficialDestination::ListeningLog) {
        // The web player's log host (docs/api/explicit-listening-reports.md §4c).
        if (protocol != Protocol::Weapi || logicalPath != u"/api/feedback/weblog")
            return std::nullopt;
        host = u"clientlogusf.music.163.com"_s;
    } else if (destination == OfficialDestination::TogetherHttp) {
        if (protocol != Protocol::Eapi)
            return std::nullopt;
        constexpr QStringView paths[]{u"/api/listen/together/room/create",
                                      u"/api/listen/together/room/check",
                                      u"/api/listen/together/play/invitation/accept",
                                      u"/api/listen/together/status/get",
                                      u"/api/listen/together/end/check",
                                      u"/api/listen/together/end/v2",
                                      u"/api/listen/together/heartbeat",
                                      u"/api/listen/together/play/command/report",
                                      u"/api/listen/together/sync/playlist/get",
                                      u"/api/listen/together/sync/list/command/report",
                                      u"/api/listen/together/restore/reconnect/info",
                                      u"/api/listen/together/device/reconnect/notice"};
        bool permitted = false;
        for (const auto path : paths)
            if (logicalPath == path)
                permitted = true;
        if (!permitted)
            return std::nullopt;
        host = u"interface.music.163.com"_s;
    } else if (destination != OfficialDestination::Default)
        return std::nullopt;
    QUrl url;
    url.setScheme(u"https"_s);
    url.setHost(host);
    url.setPath(prefix + logicalPath.sliced(5).toString());
    return url;
}

} // namespace cirrus::netease
