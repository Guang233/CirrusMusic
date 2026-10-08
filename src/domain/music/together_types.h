#pragma once
#include "domain/music/music_types.h"
#include <QList>
#include <QMetaType>
#include <QUuid>
#include <algorithm>
#include <optional>
#include <variant>

namespace cirrus::domain
{
struct TogetherRoomId {
    QString value;
    bool isValid() const
    {
        if (value.isEmpty() || value.size() > 128)
            return false;
        for (auto c : value)
            if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
                  (c >= u'0' && c <= u'9') || c == u'_' || c == u'-'))
                return false;
        return true;
    }
    friend bool operator==(const TogetherRoomId&, const TogetherRoomId&) = default;
};
// Physical chat-room metadata, NOT an IM credential or business room identifier.
struct TogetherChatRoomId {
    QString value;
};
inline bool validTogetherCounter(qint64 n) { return n >= 0 && n <= 9007199254740991LL; }
inline bool validTogetherProgress(qint64 n) { return n >= 0 && n <= 604800000; }
enum class TogetherPlayStatus { Play, Pause };
enum class TogetherPlayAction { Play, Pause, Next, Previous, Goto, Progress };
struct TogetherCreate {
};
struct TogetherCheck {
    TogetherRoomId room;
};
struct TogetherJoin {
    TogetherRoomId room;
    QString inviterId;
};
struct TogetherStatus {
};
struct TogetherEndCheck {
    TogetherRoomId room;
};
struct TogetherEnd {
    TogetherRoomId room;
};
struct TogetherHeartbeat {
    TogetherRoomId room;
    SongId song;
    TogetherPlayStatus status{};
    qint64 progressMs = 0;
};
struct TogetherPlay {
    TogetherRoomId room;
    TogetherPlayAction action{};
    SongId former, target;
    TogetherPlayStatus status{};
    qint64 progressMs = 0, clientSeq = 0;
};
struct TogetherQueue {
    TogetherRoomId room;
};
// REPLACE only. Version is a caller-supplied counter, NOT a CAS receipt.
struct TogetherReplaceQueue {
    TogetherRoomId room;
    qint64 version = 0;
    QList<SongId> display, random;
    qint64 clientSeq = 0;
};
struct TogetherReconnectInfo {
};
struct TogetherDeviceNotice {
    TogetherRoomId room;
};
using TogetherAction =
    std::variant<TogetherCreate, TogetherCheck, TogetherJoin, TogetherStatus, TogetherEndCheck,
                 TogetherEnd, TogetherHeartbeat, TogetherPlay, TogetherQueue, TogetherReplaceQueue,
                 TogetherReconnectInfo, TogetherDeviceNotice>;
enum class TogetherKind {
    Create,
    Check,
    Join,
    Status,
    EndCheck,
    End,
    Heartbeat,
    Play,
    Queue,
    ReplaceQueue,
    ReconnectInfo,
    DeviceNotice
};
inline TogetherKind togetherKind(const TogetherAction& a) { return TogetherKind(a.index()); }
inline bool validTogetherAction(const TogetherAction& action)
{
    return std::visit(
        [](const auto& a) {
            if constexpr (requires { a.room; })
                if (!a.room.isValid())
                    return false;
            if constexpr (requires { a.inviterId; })
                if (!validMusicId(a.inviterId))
                    return false;
            if constexpr (requires { a.progressMs; })
                if (!validTogetherProgress(a.progressMs) || a.status < TogetherPlayStatus::Play ||
                    a.status > TogetherPlayStatus::Pause)
                    return false;
            if constexpr (requires { a.song; })
                if (!validMusicId(a.song.value))
                    return false;
            if constexpr (requires { a.former; })
                if (!validTogetherCounter(a.clientSeq) || !validMusicId(a.former.value) ||
                    !validMusicId(a.target.value) || a.action < TogetherPlayAction::Play ||
                    a.action > TogetherPlayAction::Progress)
                    return false;
            if constexpr (requires { a.version; }) {
                if (!validTogetherCounter(a.version) || !validTogetherCounter(a.clientSeq) ||
                    a.display.isEmpty() || a.display.size() > 1000 ||
                    a.random.size() != a.display.size())
                    return false;
                QList<QString> d, r;
                for (const auto& id : a.display) {
                    if (!validMusicId(id.value))
                        return false;
                    d.append(id.value);
                }
                for (const auto& id : a.random) {
                    if (!validMusicId(id.value))
                        return false;
                    r.append(id.value);
                }
                std::sort(d.begin(), d.end());
                std::sort(r.begin(), r.end());
                if (d != r)
                    return false;
            }
            return true;
        },
        action);
}
struct TogetherRequest {
    QUuid eventId = QUuid::createUuid();
    quint64 roomGeneration = 0; // caller's local correlation; no automatic room transition fence
    TogetherAction action;
    bool isValid() const
    {
        return !eventId.isNull() && roomGeneration && validTogetherAction(action);
    }
};
enum class TogetherOutcome { NotSent, Acknowledged, Rejected, Unknown };
enum class TogetherError {
    None,
    InvalidRequest,
    AuthenticationRequired,
    StaleContext,
    Canceled,
    Busy,
    InvalidResponse,
    ProviderRejected,
    Timeout,
    Network,
    ProtocolFailure,
    RateLimited
};
struct TogetherRoom {
    TogetherRoomId id;
    std::optional<TogetherChatRoomId> chatRoom;
    std::optional<QString> imType, roomType;
    std::optional<bool> mainDevice, canJoin, ended;
};
struct TogetherReportedPlay {
    TogetherPlayAction action{};
    TogetherPlayStatus status{};
    SongId former, target;
    qint64 progressMs = 0, clientSeq = 0, serverSeq = 0;
};
struct TogetherReconnect {
    bool canReconnect = false;
    std::optional<TogetherRoomId> room;
    std::optional<bool> mainDevice, requiresDeviceConfirmation, supportsMultiDevice;
};
struct TogetherResult {
    QUuid eventId;
    TogetherOutcome outcome = TogetherOutcome::NotSent;
    TogetherError error{};
    TogetherKind kind{};
    quint64 roomGeneration = 0;
    std::optional<qint64> providerCode, retryAfterMs;
    std::optional<TogetherRoom> room;
    std::optional<bool> inRoom, joinable, endAllowed, heartbeatAccepted;
    std::optional<TogetherReportedPlay> reportedPlay;
    std::optional<TogetherReconnect> reconnect;
    bool playlistContentUnmapped = false; // never a complete queue or permission snapshot
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::TogetherRequest)
Q_DECLARE_METATYPE(cirrus::domain::TogetherResult)
