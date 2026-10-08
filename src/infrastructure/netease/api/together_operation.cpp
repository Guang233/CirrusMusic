#include "together_operation.h"
#include "domain/music/together_types.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace cirrus::netease
{
using namespace domain;
bool isTogetherOperation(ApiOperation op)
{
    return op >= ApiOperation::TogetherCreate && op <= ApiOperation::TogetherDeviceNotice;
}
std::optional<ApiOperationContract> togetherContract(ApiOperation op)
{
    if (!isTogetherOperation(op))
        return std::nullopt;
    const QStringList suffixes{QStringLiteral("room/create"),
                               QStringLiteral("room/check"),
                               QStringLiteral("play/invitation/accept"),
                               QStringLiteral("status/get"),
                               QStringLiteral("end/check"),
                               QStringLiteral("end/v2"),
                               QStringLiteral("heartbeat"),
                               QStringLiteral("play/command/report"),
                               QStringLiteral("sync/playlist/get"),
                               QStringLiteral("sync/list/command/report"),
                               QStringLiteral("restore/reconnect/info"),
                               QStringLiteral("device/reconnect/notice")};
    return ApiOperationContract{QStringLiteral("/api/listen/together/") +
                                    suffixes[int(op) - int(ApiOperation::TogetherCreate)],
                                Protocol::Eapi, ResponseEncoding::Json,
                                op == ApiOperation::TogetherQueue ? 512 * 1024 : 64 * 1024,
                                OfficialDestination::TogetherHttp};
}
namespace
{
std::optional<qint64> counter(const QJsonValue& v)
{
    if (!v.isDouble())
        return std::nullopt;
    const auto n = v.toInteger(-1);
    return validTogetherCounter(n) && v == QJsonValue(n) ? std::optional<qint64>(n) : std::nullopt;
}
bool id(const QJsonValue& v) { return v.isString() && validMusicId(v.toString()); }
bool room(const QJsonValue& v) { return v.isString() && TogetherRoomId{v.toString()}.isValid(); }
bool status(const QJsonValue& v)
{
    return v == QJsonValue(QStringLiteral("PLAY")) || v == QJsonValue(QStringLiteral("PAUSE"));
}
std::optional<QJsonObject> objectString(const QJsonValue& v)
{
    if (!v.isString())
        return std::nullopt;
    const auto text = v.toString();
    if (!text.isValidUtf16() || text.contains(QChar::Null) || text.toUtf8().size() > 256 * 1024)
        return std::nullopt;
    QJsonParseError e;
    const auto d = QJsonDocument::fromJson(text.toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject())
        return std::nullopt;
    return d.object();
}
bool songs(const QJsonValue& v, QList<SongId>& out)
{
    if (!v.isArray() || v.toArray().size() > 1000)
        return false;
    for (auto a : v.toArray()) {
        if (!id(a))
            return false;
        out.append({a.toString()});
    }
    return true;
}
} // namespace
std::optional<QJsonObject> togetherPayload(ApiOperation op, const QJsonObject& p)
{
    if (!isTogetherOperation(op))
        return std::nullopt;
    if (op == ApiOperation::TogetherCreate) {
        if (!p.isEmpty())
            return std::nullopt;
        return QJsonObject{{QStringLiteral("refer"), QStringLiteral("songplay_more")}};
    }
    if (op == ApiOperation::TogetherStatus || op == ApiOperation::TogetherReconnectInfo)
        return p.isEmpty() ? std::optional<QJsonObject>(QJsonObject{}) : std::nullopt;
    if (op == ApiOperation::TogetherReplaceQueue) {
        if (p.size() != 1)
            return std::nullopt;
        const auto parsed = objectString(p.value(QStringLiteral("command")));
        if (!parsed)
            return std::nullopt;
        const auto& c = *parsed;
        if (c.size() != 7 ||
            c.value(QStringLiteral("commandType")) != QJsonValue(QStringLiteral("REPLACE")) ||
            c.value(QStringLiteral("anchorSongId")) != QJsonValue(QString()) ||
            c.value(QStringLiteral("anchorPosition")) != QJsonValue(-1) ||
            !counter(c.value(QStringLiteral("clientSeq"))))
            return std::nullopt;
        const auto versions = c.value(QStringLiteral("version"));
        if (!versions.isArray() || versions.toArray().size() != 1 ||
            !versions.toArray().first().isObject())
            return std::nullopt;
        const auto ver = versions.toArray().first().toObject();
        const auto n = counter(ver.value(QStringLiteral("version")));
        if (ver.size() != 2 || !id(ver.value(QStringLiteral("userId"))) || !n)
            return std::nullopt;
        TogetherReplaceQueue q{{QStringLiteral("validation")}, *n};
        if (!songs(c.value(QStringLiteral("displayList")), q.display) ||
            !songs(c.value(QStringLiteral("randomList")), q.random) || !validTogetherAction(q))
            return std::nullopt;
        return p; // document's command string, NOT legacy playlistParam or a nested object
    }
    if (!room(p.value(QStringLiteral("roomId"))))
        return std::nullopt;
    if (op == ApiOperation::TogetherJoin) {
        if (p.size() != 2 || !id(p.value(QStringLiteral("inviterId"))))
            return std::nullopt;
        auto out = p;
        out.insert(QStringLiteral("refer"), QStringLiteral("inbox_invite"));
        return out;
    }
    if (op == ApiOperation::TogetherHeartbeat) {
        const auto n = counter(p.value(QStringLiteral("progress")));
        if (p.size() != 4 || !id(p.value(QStringLiteral("songId"))) ||
            !status(p.value(QStringLiteral("playStatus"))) || !n || !validTogetherProgress(*n))
            return std::nullopt;
        return p;
    }
    if (op == ApiOperation::TogetherPlay) {
        if (p.size() != 2)
            return std::nullopt;
        const auto parsed = objectString(p.value(QStringLiteral("commandInfo")));
        if (!parsed)
            return std::nullopt;
        const auto& c = *parsed;
        const auto n = counter(c.value(QStringLiteral("progress")));
        const QStringList commands{QStringLiteral("PLAY"), QStringLiteral("PAUSE"),
                                   QStringLiteral("NEXT"), QStringLiteral("PREVIOUS"),
                                   QStringLiteral("GOTO"), QStringLiteral("PROGRESS")};
        if (c.size() != 6 || !c.value(QStringLiteral("commandType")).isString() ||
            !commands.contains(c.value(QStringLiteral("commandType")).toString()) || !n ||
            !validTogetherProgress(*n) || !counter(c.value(QStringLiteral("clientSeq"))) ||
            !status(c.value(QStringLiteral("playStatus"))) ||
            !id(c.value(QStringLiteral("formerSongId"))) ||
            !id(c.value(QStringLiteral("targetSongId"))))
            return std::nullopt;
        return p;
    }
    return p.size() == 1 ? std::optional<QJsonObject>(p) : std::nullopt;
}
} // namespace cirrus::netease
