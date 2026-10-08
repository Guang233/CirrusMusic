#include "together_gateway.h"
#include "metadata_mapping.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <utility>

namespace cirrus::netease
{
using namespace domain;
namespace
{
QString statusName(TogetherPlayStatus s)
{
    return s == TogetherPlayStatus::Play ? QStringLiteral("PLAY") : QStringLiteral("PAUSE");
}
QString actionName(TogetherPlayAction a)
{
    const QStringList names{QStringLiteral("PLAY"), QStringLiteral("PAUSE"),
                            QStringLiteral("NEXT"), QStringLiteral("PREVIOUS"),
                            QStringLiteral("GOTO"), QStringLiteral("PROGRESS")};
    return names[int(a)];
}
QString json(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QJsonArray songArray(const QList<SongId>& ids)
{
    QJsonArray a;
    for (const auto& id : ids)
        a.append(id.value);
    return a;
}
std::optional<qint64> number(const QJsonValue& v)
{
    if (!v.isDouble())
        return std::nullopt;
    const auto n = v.toInteger(-1);
    return validTogetherCounter(n) && QJsonValue(n) == v ? std::optional<qint64>(n) : std::nullopt;
}
bool optionalBool(const QJsonValue& v, std::optional<bool>& out)
{
    if (v.isUndefined() || v.isNull())
        return true;
    if (!v.isBool())
        return false;
    out = v.toBool();
    return true;
}
std::optional<TogetherRoom> roomFrom(const QJsonValue& v)
{
    if (!v.isObject())
        return std::nullopt;
    const auto o = v.toObject();
    const auto id = o.value(QStringLiteral("roomId"));
    if (!id.isString() || !TogetherRoomId{id.toString()}.isValid())
        return std::nullopt;
    TogetherRoom r{{id.toString()}};
    const auto chat = o.value(QStringLiteral("chatRoomId"));
    if (!chat.isNull() && !chat.isUndefined()) {
        if (!chat.isString() || !TogetherRoomId{chat.toString()}.isValid())
            return std::nullopt;
        r.chatRoom = TogetherChatRoomId{chat.toString()};
    }
    if (!mapping::metadataText(o.value(QStringLiteral("imType")), r.imType) ||
        !mapping::metadataText(o.value(QStringLiteral("roomType")), r.roomType) ||
        !optionalBool(o.value(QStringLiteral("mainDevice")), r.mainDevice) ||
        !optionalBool(o.value(QStringLiteral("roomCanJoin")), r.canJoin) ||
        !optionalBool(o.value(QStringLiteral("roomEnd")), r.ended))
        return std::nullopt;
    return r;
}
std::optional<TogetherReportedPlay> reportedPlay(const QJsonValue& v)
{
    if (!v.isObject())
        return std::nullopt;
    const auto o = v.toObject();
    const auto progress = number(o.value(QStringLiteral("progress")));
    const auto client = number(o.value(QStringLiteral("clientSeq")));
    const auto server = number(o.value(QStringLiteral("serverSeq")));
    const auto former = o.value(QStringLiteral("formerSongId"));
    const auto target = o.value(QStringLiteral("targetSongId"));
    int action = -1;
    for (int i = 0; i < 6; ++i)
        if (o.value(QStringLiteral("commandType")) == QJsonValue(actionName(TogetherPlayAction(i))))
            action = i;
    const auto status = o.value(QStringLiteral("playStatus"));
    if (action < 0 ||
        (status != QJsonValue(QStringLiteral("PLAY")) &&
         status != QJsonValue(QStringLiteral("PAUSE"))) ||
        !progress || !validTogetherProgress(*progress) || !client || !server ||
        !former.isString() || !validMusicId(former.toString()) || !target.isString() ||
        !validMusicId(target.toString()))
        return std::nullopt;
    return TogetherReportedPlay{TogetherPlayAction(action),
                                status == QJsonValue(QStringLiteral("PLAY"))
                                    ? TogetherPlayStatus::Play
                                    : TogetherPlayStatus::Pause,
                                {former.toString()},
                                {target.toString()},
                                *progress,
                                *client,
                                *server};
}
bool details(const TogetherRequest& request, const QJsonObject& root, TogetherResult& r)
{
    const auto raw = root.value(QStringLiteral("data"));
    if (!raw.isObject())
        return false;
    const auto o = raw.toObject();
    const auto kind = togetherKind(request.action);
    if (kind == TogetherKind::Create || kind == TogetherKind::Join ||
        kind == TogetherKind::Status) {
        if (kind == TogetherKind::Status) {
            const auto in = o.value(QStringLiteral("inRoom"));
            if (!in.isBool())
                return false;
            r.inRoom = in.toBool();
            if (!*r.inRoom)
                return o.value(QStringLiteral("roomInfo")).isNull() ||
                       o.value(QStringLiteral("roomInfo")).isUndefined();
        }
        r.room = roomFrom(o.value(QStringLiteral("roomInfo")));
        if (!r.room)
            return false;
        if (const auto* join = std::get_if<TogetherJoin>(&request.action))
            return join->room == r.room->id;
        return true;
    }
    if (kind == TogetherKind::Check || kind == TogetherKind::EndCheck ||
        kind == TogetherKind::Heartbeat) {
        const auto key =
            kind == TogetherKind::Check ? QStringLiteral("joinable") : QStringLiteral("result");
        const auto b = o.value(key);
        if (!b.isBool())
            return false;
        if (kind == TogetherKind::Check)
            r.joinable = b.toBool();
        else if (kind == TogetherKind::EndCheck)
            r.endAllowed = b.toBool();
        else
            r.heartbeatAccepted = b.toBool();
        // A false decision is valid read metadata, not activation or transport failure.
        return true;
    }
    if (kind == TogetherKind::Queue) {
        const auto wrapper = o.value(QStringLiteral("playList"));
        if (!wrapper.isObject())
            return false;
        const auto p = wrapper.toObject();
        if (!p.value(QStringLiteral("playlist")).isObject())
            return false;
        r.playlistContentUnmapped = true; // document does not define this object's song/list schema
        const auto play = p.value(QStringLiteral("playCommand"));
        if (!play.isNull() && !play.isUndefined()) {
            r.reportedPlay = reportedPlay(play);
            if (!r.reportedPlay)
                return false;
        }
        return true;
    }
    if (kind == TogetherKind::ReconnectInfo) {
        const auto can = o.value(QStringLiteral("canReconnect"));
        if (!can.isBool())
            return false;
        TogetherReconnect c;
        c.canReconnect = can.toBool();
        const auto id = o.value(QStringLiteral("roomId"));
        if (!id.isNull() && !id.isUndefined() && id != QJsonValue(QString())) {
            if (!id.isString() || !TogetherRoomId{id.toString()}.isValid())
                return false;
            c.room = TogetherRoomId{id.toString()};
        }
        if ((c.canReconnect && !c.room) ||
            !optionalBool(o.value(QStringLiteral("mainDevice")), c.mainDevice) ||
            !optionalBool(o.value(QStringLiteral("needDeviceReconnectConfirm")),
                          c.requiresDeviceConfirmation) ||
            !optionalBool(o.value(QStringLiteral("supportMultiDeviceReconnect")),
                          c.supportsMultiDevice))
            return false;
        r.reconnect = c;
        return true;
    }
    if (kind == TogetherKind::End) {
        if (o.isEmpty())
            return true;
        const auto code = number(o.value(QStringLiteral("code")));
        return code && *code == 200;
    }
    // Play/list GeneralResult needs its boolean result; device-notice has no
    // established result schema and only acknowledges the HTTP envelope.
    const auto result = o.value(QStringLiteral("result"));
    if ((kind == TogetherKind::Play || kind == TogetherKind::ReplaceQueue) && !result.isBool())
        return false;
    if (!result.isUndefined()) {
        if (!result.isBool())
            return false;
        if (!result.toBool()) {
            r.outcome = TogetherOutcome::Rejected;
            r.error = TogetherError::ProviderRejected;
        }
    }
    const auto innerCode = o.value(QStringLiteral("code"));
    if (!innerCode.isUndefined()) {
        const auto n = number(innerCode);
        if (!n)
            return false;
        if (*n != 200) {
            r.outcome = TogetherOutcome::Rejected;
            r.error = TogetherError::ProviderRejected;
        }
    }
    return true;
}
} // namespace
std::optional<TogetherWireRequest> togetherRequest(const TogetherRequest& request,
                                                   QStringView account)
{
    if (!request.isValid() || !validMusicId(account))
        return std::nullopt;
    TogetherWireRequest out{
        ApiOperation(int(ApiOperation::TogetherCreate) + int(togetherKind(request.action))), {}};
    std::visit(
        [&](const auto& a) {
            if constexpr (requires { a.room; })
                out.parameters.insert(QStringLiteral("roomId"), a.room.value);
            if constexpr (requires { a.inviterId; })
                out.parameters.insert(QStringLiteral("inviterId"), a.inviterId);
            if constexpr (requires { a.song; }) {
                out.parameters.insert(QStringLiteral("songId"), a.song.value);
                out.parameters.insert(QStringLiteral("playStatus"), statusName(a.status));
                out.parameters.insert(QStringLiteral("progress"), a.progressMs);
            }
            if constexpr (requires { a.former; }) {
                const QJsonObject c{{QStringLiteral("commandType"), actionName(a.action)},
                                    {QStringLiteral("formerSongId"), a.former.value},
                                    {QStringLiteral("targetSongId"), a.target.value},
                                    {QStringLiteral("progress"), a.progressMs},
                                    {QStringLiteral("playStatus"), statusName(a.status)},
                                    {QStringLiteral("clientSeq"), a.clientSeq}};
                out.parameters.insert(QStringLiteral("commandInfo"), json(c));
            }
            if constexpr (requires { a.version; }) {
                const QJsonObject c{
                    {QStringLiteral("commandType"), QStringLiteral("REPLACE")},
                    {QStringLiteral("anchorSongId"), QString()},
                    {QStringLiteral("anchorPosition"), -1},
                    {QStringLiteral("clientSeq"), a.clientSeq},
                    {QStringLiteral("displayList"), songArray(a.display)},
                    {QStringLiteral("randomList"), songArray(a.random)},
                    {QStringLiteral("version"),
                     QJsonArray{QJsonObject{{QStringLiteral("userId"), account.toString()},
                                            {QStringLiteral("version"), a.version}}}}};
                out.parameters = {{QStringLiteral("command"), json(c)}};
            }
        },
        request.action);
    return out;
}
TogetherResult togetherResult(const TogetherRequest& request, const ApiResult& api)
{
    TogetherResult r;
    r.eventId = request.eventId;
    r.kind = togetherKind(request.action);
    r.roomGeneration = request.roomGeneration;
    r.outcome = api.submittedToTransport ? TogetherOutcome::Unknown : TogetherOutcome::NotSent;
    if (api.error == ApiError::None && api.decoded.ok() && api.decoded.businessCode) {
        r.providerCode = api.decoded.businessCode;
        r.outcome =
            *r.providerCode == 200 ? TogetherOutcome::Acknowledged : TogetherOutcome::Rejected;
        r.error = *r.providerCode == 200 ? TogetherError::None : TogetherError::ProviderRejected;
        if (*r.providerCode == 200 && !details(request, api.decoded.object, r)) {
            r = TogetherResult{request.eventId,
                               TogetherOutcome::Unknown,
                               TogetherError::InvalidResponse,
                               togetherKind(request.action),
                               request.roomGeneration,
                               api.decoded.businessCode};
        }
    } else if (api.error == ApiError::Canceled)
        r.error = TogetherError::Canceled;
    else if (api.error == ApiError::StaleContext)
        r.error = TogetherError::StaleContext;
    else if (api.error == ApiError::Busy)
        r.error = TogetherError::Busy;
    else if (api.error == ApiError::InvalidRequest)
        r.error = TogetherError::InvalidRequest;
    else if (api.error == ApiError::Decode || api.error == ApiError::None)
        r.error = TogetherError::InvalidResponse;
    else if (api.transportError == TransportError::Timeout)
        r.error = TogetherError::Timeout;
    else if (api.error == ApiError::Transport)
        r.error = TogetherError::Network;
    else
        r.error = TogetherError::ProtocolFailure;
    if (api.httpStatus == 429 || api.decoded.businessCode == 429) {
        r.error = TogetherError::RateLimited;
        bool ok = false;
        const auto seconds = api.retryAfter.toLongLong(&ok);
        if (ok && seconds >= 0 && seconds <= 86400)
            r.retryAfterMs = seconds * 1000;
    }
    return r;
}
TogetherGateway::TogetherGateway(application::SessionCoordinator& session, SessionApiClient& client,
                                 QObject* parent)
    : ITogetherGateway(parent), m_session(session), m_client(&client)
{
    Q_ASSERT(session.thread() == thread() && client.thread() == thread());
    connect(
        &client, &SessionApiClient::completed, this,
        [this](quint64 apiId, SessionToken token, const ApiResult& api) {
            const auto id = m_apiIds.value(apiId);
            if (!m_pending.contains(id))
                return;
            const auto p = m_pending[id];
            auto r = togetherResult(p.request, api);
            if (token != p.context || !current(p) || p.forced != TogetherError::None) {
                r = {p.request.eventId,
                     api.submittedToTransport ? TogetherOutcome::Unknown : TogetherOutcome::NotSent,
                     !current(p) || token != p.context ? TogetherError::StaleContext : p.forced};
            }
            finish(id, r);
        });
    connect(&session, &application::SessionCoordinator::changed, this, [this](SessionSnapshot s) {
        for (auto id : m_pending.keys()) {
            const auto p = m_pending.value(id);
            if (s.token != p.context || s.phase != SessionPhase::Authenticated ||
                s.accountId != p.account || !current(p))
                cancelWith(id, TogetherError::StaleContext);
        }
    });
    connect(&client, &QObject::destroyed, this, [this] {
        for (auto id : m_pending.keys())
            finish(id, {{},
                        m_pending[id].apiId ? TogetherOutcome::Unknown : TogetherOutcome::NotSent,
                        TogetherError::Network});
    });
}
TogetherGateway::~TogetherGateway()
{
    if (!m_client)
        return;
    disconnect(m_client, nullptr, this, nullptr);
    for (auto apiId : m_apiIds.keys())
        m_client->cancel(apiId);
}
bool TogetherGateway::current(const Pending& p) const
{
    const auto s = m_session.snapshot();
    return m_client && m_session.accepts(p.context) && s.phase == SessionPhase::Authenticated &&
           p.account == s.accountId;
}
quint64 TogetherGateway::submit(SessionToken context, QString account, TogetherRequest request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 8)
        return 0;
    for (const auto& p : std::as_const(m_pending))
        if (!request.eventId.isNull() && p.request.eventId == request.eventId)
            return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, std::move(account), std::move(request)});
    QTimer::singleShot(0, this, [this, id] { start(id); });
    return id;
}
void TogetherGateway::start(quint64 id)
{
    if (!m_pending.contains(id))
        return;
    auto& p = m_pending[id];
    TogetherError error = p.forced;
    if (error == TogetherError::None) {
        if (!m_session.accepts(p.context))
            error = TogetherError::StaleContext;
        else if (m_session.snapshot().phase != SessionPhase::Authenticated)
            error = TogetherError::AuthenticationRequired;
        else if (!p.request.isValid() || !validMusicId(p.account) ||
                 p.account != m_session.snapshot().accountId)
            error = TogetherError::InvalidRequest;
        else if (!m_client)
            error = TogetherError::Network;
    }
    if (error != TogetherError::None) {
        finish(id, {{}, TogetherOutcome::NotSent, error});
        return;
    }
    const auto wire = togetherRequest(p.request, p.account);
    p.apiId = m_client->submit(p.context, wire->operation, wire->parameters);
    m_apiIds.insert(p.apiId, id);
}
void TogetherGateway::cancelWith(quint64 id, TogetherError error)
{
    if (!m_pending.contains(id))
        return;
    auto& p = m_pending[id];
    p.forced = error;
    if (p.apiId && m_client)
        m_client->cancel(p.apiId);
    else
        QTimer::singleShot(
            0, this, [this, id, error] { finish(id, {{}, TogetherOutcome::NotSent, error}); });
}
void TogetherGateway::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelWith(id, TogetherError::Canceled);
}
void TogetherGateway::finish(quint64 id, TogetherResult result)
{
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.take(id);
    m_apiIds.remove(p.apiId);
    result.eventId = p.request.eventId;
    result.kind = togetherKind(p.request.action);
    result.roomGeneration = p.request.roomGeneration;
    emit completed(id, p.context, result);
}
} // namespace cirrus::netease
