#include "together_operation.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace cirrus::netease
{
using namespace domain;
namespace
{
const QStringList& actionNames()
{
    static const QStringList n{QStringLiteral("PLAY"), QStringLiteral("PAUSE"),
                               QStringLiteral("NEXT"), QStringLiteral("PREVIOUS"),
                               QStringLiteral("GOTO"), QStringLiteral("PROGRESS")};
    return n;
}
const QStringList& statusNames()
{
    static const QStringList n{QStringLiteral("PLAY"), QStringLiteral("PAUSE")};
    return n;
}
const QStringList& modeNames()
{
    static const QStringList n{QStringLiteral("ORDER_LOOP"), QStringLiteral("RANDOM"),
                               QStringLiteral("SINGLE_LOOP")};
    return n;
}
const QStringList& triggerNames()
{
    static const QStringList n{QStringLiteral("AUTO"), QStringLiteral("MANUAL")};
    return n;
}
std::optional<int> nameIndex(const QJsonValue& v, const QStringList& names)
{
    const auto i = v.isString() ? names.indexOf(v.toString()) : -1;
    return i >= 0 ? std::optional<int>(i) : std::nullopt;
}
QString json(const QJsonValue& v)
{
    const auto d = v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject());
    return QString::fromUtf8(d.toJson(QJsonDocument::Compact));
}
// A parameter that carries JSON text (commandInfo, playlistParam, playlistVersion).
QJsonValue jsonText(const QJsonValue& v)
{
    if (!v.isString())
        return {};
    const auto bytes = v.toString().toUtf8();
    if (bytes.size() > 256 * 1024)
        return {};
    QJsonParseError e;
    const auto d = QJsonDocument::fromJson(bytes, &e);
    if (e.error != QJsonParseError::NoError)
        return {};
    return d.isArray() ? QJsonValue(d.array()) : QJsonValue(d.object());
}
QJsonArray songArray(const QList<SongId>& ids)
{
    QJsonArray a;
    for (const auto& id : ids)
        a.append(id.value);
    return a;
}
QJsonArray versionArray(const QList<TogetherVersion>& versions)
{
    QJsonArray a;
    for (const auto& v : versions)
        a.append(QJsonObject{{QStringLiteral("userId"), v.userId.toLongLong()},
                             {QStringLiteral("version"), v.version}});
    return a;
}
// PlaylistCommand. Queue describes the own list (no command type, clientSeq 0); REPLACE adds
// the type, its client sequence and the versions.
QJsonObject listObject(const TogetherList& l, const TogetherReplaceQueue* replace)
{
    QJsonObject o{{QStringLiteral("anchorPosition"), l.anchorPosition},
                  {QStringLiteral("clientSeq"), replace ? replace->clientSeq : 0},
                  {QStringLiteral("displayList"), songArray(l.display)},
                  {QStringLiteral("playMode"), modeNames()[int(l.mode)]}};
    if (!l.anchor.value.isEmpty())
        o.insert(QStringLiteral("anchorSongId"), l.anchor.value);
    if (!l.random.isEmpty())
        o.insert(QStringLiteral("randomList"), songArray(l.random));
    if (replace) {
        o.insert(QStringLiteral("commandType"), QStringLiteral("REPLACE"));
        o.insert(QStringLiteral("version"), versionArray(replace->versions));
    }
    return o;
}
// Lenient reads: togetherPayload re-renders what they produce and compares.
QString text(const QJsonObject& o, QStringView key) { return o.value(key).toString(); }
QList<SongId> songs(const QJsonValue& v)
{
    QList<SongId> out;
    for (const auto& a : v.toArray())
        out.append({a.toString()});
    return out;
}
qint64 integer(const QJsonValue& v) { return togetherCounter(v).value_or(-1); }
qint64 decimal(const QJsonValue& v)
{
    bool ok = false;
    const auto n = v.toString().toLongLong(&ok);
    return ok ? n : -1;
}
TogetherList listFrom(const QJsonObject& o)
{
    TogetherList l;
    l.anchor = {text(o, u"anchorSongId")};
    l.anchorPosition = o.value(QStringLiteral("anchorPosition")).toInteger(-2);
    l.mode =
        togetherPlayMode(o.value(QStringLiteral("playMode"))).value_or(TogetherPlayMode::OrderLoop);
    l.display = songs(o.value(QStringLiteral("displayList")));
    l.random = songs(o.value(QStringLiteral("randomList")));
    return l;
}
std::optional<TogetherAction> actionFrom(ApiOperation op, const QJsonObject& p)
{
    const TogetherRoomId room{text(p, u"roomId")};
    switch (TogetherKind(int(op) - int(ApiOperation::TogetherCreate))) {
    case TogetherKind::Create:
        return TogetherCreate{};
    case TogetherKind::Check:
        return TogetherCheck{room};
    case TogetherKind::Join:
        return TogetherJoin{room, text(p, u"inviterId")};
    case TogetherKind::Status:
        return TogetherStatus{};
    case TogetherKind::EndCheck:
        return TogetherEndCheck{room};
    case TogetherKind::End:
        return TogetherEnd{room};
    case TogetherKind::Heartbeat: {
        const auto versions =
            togetherVersions(jsonText(p.value(QStringLiteral("playlistVersion"))));
        if (!versions)
            return std::nullopt;
        return TogetherHeartbeat{room,
                                 {text(p, u"songId")},
                                 togetherPlayStatus(p.value(QStringLiteral("playStatus")))
                                     .value_or(TogetherPlayStatus::Play),
                                 decimal(p.value(QStringLiteral("progress"))),
                                 *versions};
    }
    case TogetherKind::Play: {
        const auto c = jsonText(p.value(QStringLiteral("commandInfo"))).toObject();
        const auto action = togetherPlayAction(c.value(QStringLiteral("commandType")));
        const auto status = togetherPlayStatus(c.value(QStringLiteral("playStatus")));
        const auto trigger = togetherTrigger(c.value(QStringLiteral("triggerType")));
        if (!action || !status || !trigger)
            return std::nullopt;
        return TogetherPlay{room,
                            *action,
                            {text(c, u"formerSongId")},
                            {text(c, u"targetSongId")},
                            *status,
                            integer(c.value(QStringLiteral("progress"))),
                            integer(c.value(QStringLiteral("clientSeq"))),
                            *trigger};
    }
    case TogetherKind::Queue:
        return TogetherQueue{
            room, listFrom(jsonText(p.value(QStringLiteral("playlistParam"))).toObject())};
    case TogetherKind::ReplaceQueue: {
        const auto c = jsonText(p.value(QStringLiteral("playlistParam"))).toObject();
        const auto versions = togetherVersions(c.value(QStringLiteral("version")));
        if (!versions)
            return std::nullopt;
        return TogetherReplaceQueue{room, listFrom(c), *versions,
                                    decimal(p.value(QStringLiteral("clientSeq")))};
    }
    case TogetherKind::ReconnectInfo:
        return TogetherReconnectInfo{};
    case TogetherKind::DeviceNotice:
        return TogetherDeviceNotice{room};
    case TogetherKind::SyncNotice:
        return TogetherSyncNotice{room};
    }
    return std::nullopt;
}
} // namespace
bool isTogetherOperation(ApiOperation op)
{
    return op >= ApiOperation::TogetherCreate && op <= ApiOperation::TogetherSyncNotice;
}
ApiOperation togetherOperation(TogetherKind kind)
{
    return ApiOperation(int(ApiOperation::TogetherCreate) + int(kind));
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
                               QStringLiteral("device/reconnect/notice"),
                               QStringLiteral("sync/notice")};
    return ApiOperationContract{QStringLiteral("/api/listen/together/") +
                                    suffixes[int(op) - int(ApiOperation::TogetherCreate)],
                                Protocol::Eapi, ResponseEncoding::Json,
                                op == ApiOperation::TogetherQueue ? 512 * 1024 : 64 * 1024,
                                OfficialDestination::TogetherHttp};
}
QJsonObject togetherParameters(const TogetherAction& action)
{
    QJsonObject p;
    std::visit(
        [&](const auto& a) {
            using A = std::decay_t<decltype(a)>;
            if constexpr (requires { a.room; })
                p.insert(QStringLiteral("roomId"), a.room.value);
            if constexpr (std::is_same_v<A, TogetherCreate>)
                p.insert(QStringLiteral("inviteUid"), QString());
            if constexpr (std::is_same_v<A, TogetherJoin>)
                p.insert(QStringLiteral("inviterId"), a.inviterId);
            if constexpr (std::is_same_v<A, TogetherEnd>) {
                // A user's own exit, as the app sends it (its null objects are the text "null").
                p.insert(QStringLiteral("shareInfo"), QStringLiteral("null"));
                p.insert(QStringLiteral("needRecord"), QStringLiteral("false"));
                p.insert(QStringLiteral("scene"), QString());
                p.insert(QStringLiteral("exitType"), QStringLiteral("NORMAL_END"));
                p.insert(QStringLiteral("exitReason"), QStringLiteral("null"));
            }
            if constexpr (std::is_same_v<A, TogetherHeartbeat>) {
                p.insert(QStringLiteral("songId"), a.song.value);
                p.insert(QStringLiteral("playStatus"), statusNames()[int(a.status)]);
                p.insert(QStringLiteral("progress"), QString::number(a.progressMs));
                p.insert(QStringLiteral("playlistVersion"), json(versionArray(a.versions)));
            }
            if constexpr (std::is_same_v<A, TogetherPlay>) {
                // serverSeq and userId are the service's to fill in; the app sends 0.
                const QJsonObject c{{QStringLiteral("clientSeq"), a.clientSeq},
                                    {QStringLiteral("commandType"), actionNames()[int(a.action)]},
                                    {QStringLiteral("formerSongId"), a.former.value},
                                    {QStringLiteral("playStatus"), statusNames()[int(a.status)]},
                                    {QStringLiteral("progress"), a.progressMs},
                                    {QStringLiteral("serverSeq"), 0},
                                    {QStringLiteral("targetSongId"), a.target.value},
                                    {QStringLiteral("triggerType"), triggerNames()[int(a.trigger)]},
                                    {QStringLiteral("userId"), 0}};
                p.insert(QStringLiteral("commandInfo"), json(c));
            }
            if constexpr (std::is_same_v<A, TogetherQueue>)
                p.insert(QStringLiteral("playlistParam"), json(listObject(a.list, nullptr)));
            if constexpr (std::is_same_v<A, TogetherReplaceQueue>) {
                p.insert(QStringLiteral("playlistParam"), json(listObject(a.list, &a)));
                p.insert(QStringLiteral("clientSeq"), QString::number(a.clientSeq));
            }
        },
        action);
    return p;
}
std::optional<QJsonObject> togetherPayload(ApiOperation op, const QJsonObject& p)
{
    if (!isTogetherOperation(op))
        return std::nullopt;
    const auto action = actionFrom(op, p);
    if (!action || !validTogetherAction(*action) || togetherParameters(*action) != p)
        return std::nullopt;
    auto out = p;
    if (op == ApiOperation::TogetherCreate)
        out.insert(QStringLiteral("refer"), QStringLiteral("songplay_more"));
    if (op == ApiOperation::TogetherJoin)
        out.insert(QStringLiteral("refer"), QStringLiteral("inbox_invite"));
    return out;
}
std::optional<qint64> togetherCounter(const QJsonValue& v)
{
    if (!v.isDouble())
        return std::nullopt;
    const auto n = v.toInteger(-1);
    return validTogetherCounter(n) && v == QJsonValue(n) ? std::optional<qint64>(n) : std::nullopt;
}
std::optional<QString> togetherUser(const QJsonValue& v)
{
    const auto n = togetherCounter(v);
    if (!n)
        return std::nullopt;
    return *n ? QString::number(*n) : QString();
}
std::optional<TogetherPlayAction> togetherPlayAction(const QJsonValue& v)
{
    const auto i = nameIndex(v, actionNames());
    return i ? std::optional(TogetherPlayAction(*i)) : std::nullopt;
}
std::optional<TogetherPlayStatus> togetherPlayStatus(const QJsonValue& v)
{
    const auto i = nameIndex(v, statusNames());
    return i ? std::optional(TogetherPlayStatus(*i)) : std::nullopt;
}
std::optional<TogetherPlayMode> togetherPlayMode(const QJsonValue& v)
{
    const auto i = nameIndex(v, modeNames());
    return i ? std::optional(TogetherPlayMode(*i)) : std::nullopt;
}
std::optional<TogetherTrigger> togetherTrigger(const QJsonValue& v)
{
    const auto i = nameIndex(v, triggerNames());
    return i ? std::optional(TogetherTrigger(*i)) : std::nullopt;
}
std::optional<QList<TogetherVersion>> togetherVersions(const QJsonValue& v)
{
    if (!v.isArray())
        return std::nullopt;
    QList<TogetherVersion> out;
    for (const auto& a : v.toArray()) {
        const auto o = a.toObject();
        const auto user = togetherUser(o.value(QStringLiteral("userId")));
        const auto version = togetherCounter(o.value(QStringLiteral("version")));
        if (!a.isObject() || !user || user->isEmpty() || !version)
            return std::nullopt;
        out.append({*user, *version});
    }
    if (!validTogetherVersions(out))
        return std::nullopt;
    return out;
}
} // namespace cirrus::netease
