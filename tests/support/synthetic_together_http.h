#pragma once
#include "domain/music/together_types.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace cirrus::test
{
inline QList<domain::TogetherAction> togetherHttpActions()
{
    using namespace domain;
    TogetherRoomId room{QStringLiteral("synthetic-room")};
    return {
        TogetherCreate{},
        TogetherCheck{room},
        TogetherJoin{room, QStringLiteral("102")},
        TogetherStatus{},
        TogetherEndCheck{room},
        TogetherEnd{room},
        TogetherHeartbeat{
            room, {QStringLiteral("9007199254740993")}, TogetherPlayStatus::Play, 3000},
        TogetherPlay{room,
                     TogetherPlayAction::Progress,
                     {QStringLiteral("9007199254740993")},
                     {QStringLiteral("2")},
                     TogetherPlayStatus::Pause,
                     3000,
                     6},
        TogetherQueue{room},
        TogetherReplaceQueue{room,
                             9,
                             {{QStringLiteral("2")}, {QStringLiteral("2")}, {QStringLiteral("3")}},
                             {{QStringLiteral("3")}, {QStringLiteral("2")}, {QStringLiteral("2")}},
                             6},
        TogetherReconnectInfo{},
        TogetherDeviceNotice{room}};
}
inline QJsonObject togetherHttpObject(domain::TogetherKind kind)
{
    using namespace domain;
    QJsonObject data;
    const QJsonObject room{{"roomId", "synthetic-room"},
                           {"chatRoomId", "synthetic-chat"},
                           {"imType", "SELFHOST"},
                           {"roomType", "FRIEND"},
                           {"mainDevice", false},
                           {"roomCanJoin", true},
                           {"roomEnd", false}};
    switch (kind) {
    case TogetherKind::Create:
    case TogetherKind::Join:
        data = {{"roomInfo", room}};
        break;
    case TogetherKind::Status:
        data = {{"inRoom", true}, {"roomInfo", room}};
        break;
    case TogetherKind::Check:
        data = {{"joinable", false}, {"status", "synthetic-decision"}};
        break;
    case TogetherKind::EndCheck:
    case TogetherKind::Heartbeat:
    case TogetherKind::Play:
    case TogetherKind::ReplaceQueue:
        data = {{"result", true}};
        break;
    case TogetherKind::Queue:
        data = {
            {"playList",
             QJsonObject{{"playCommand", QJsonObject{{"commandType", "PROGRESS"},
                                                     {"formerSongId", "9007199254740993"},
                                                     {"targetSongId", "2"},
                                                     {"progress", 3000},
                                                     {"playStatus", "PAUSE"},
                                                     {"clientSeq", 6},
                                                     {"serverSeq", 88}}},
                         {"playlist", QJsonObject{{"unverifiedSyntheticField", QJsonArray{}}}}}}};
        break;
    case TogetherKind::ReconnectInfo:
        data = {{"canReconnect", true},
                {"roomId", "synthetic-room"},
                {"mainDevice", false},
                {"needDeviceReconnectConfirm", true},
                {"supportMultiDeviceReconnect", true}};
        break;
    case TogetherKind::End:
    case TogetherKind::DeviceNotice:
        break;
    }
    return {{"code", 200}, {"data", data}};
}
inline QByteArray togetherHttpFixture(domain::TogetherKind kind)
{
    return QJsonDocument(togetherHttpObject(kind)).toJson(QJsonDocument::Compact);
}
} // namespace cirrus::test
