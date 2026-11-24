// Cloud drive writes (docs/api/cloud-drive.md): delete, match, and the upload steps' payloads.
#include "domain/music/collection_types.h"
#include "infrastructure/netease/api/api_operation.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include <QJsonArray>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::netease;

namespace
{
const QString md5 = QStringLiteral("0123456789abcdef0123456789abcdef");
}

class CloudWritesTest : public QObject
{
    Q_OBJECT
  private slots:
    void domainBudgets()
    {
        QVERIFY((CloudSongDelete{{{"1"}, {"9007199254740993"}}}.isValid()));
        QVERIFY(!CloudSongDelete{}.isValid());
        QVERIFY(!(CloudSongDelete{{{"1"}, {"1"}}}.isValid()));
        QVERIFY(!CloudSongDelete{{{"01"}}}.isValid());
        QList<CloudSongId> many;
        for (int i = 1; i <= CloudSongDelete::MaximumSongs + 1; ++i)
            many.append({QString::number(i)});
        QVERIFY(!CloudSongDelete{many}.isValid());
        many.removeLast();
        QVERIFY(CloudSongDelete{many}.isValid());

        QVERIFY((CloudSongMatch{"101", {"5"}, SongId{"7"}}).isValid());
        QVERIFY((CloudSongMatch{"101", {"5"}, std::nullopt}).isValid());
        QVERIFY(!(CloudSongMatch{"", {"5"}, std::nullopt}).isValid());
        QVERIFY(!(CloudSongMatch{"101", {"x"}, std::nullopt}).isValid());
        QVERIFY(!(CloudSongMatch{"101", {"5"}, SongId{"0"}}).isValid());
        const CollectionMutation match{QUuid::createUuid(), CloudSongMatch{"101", {"5"}, {}}};
        QCOMPARE(collectionSourceAccount(match), std::optional<QString>("101"));
    }

    void contracts()
    {
        const QList<std::tuple<ApiOperation, QString, Protocol>> expected{
            {ApiOperation::CloudDelete, "/api/cloud/del", Protocol::Weapi},
            {ApiOperation::CloudMatch, "/api/cloud/user/song/match", Protocol::Weapi},
            {ApiOperation::CloudUploadCheck, "/api/cloud/upload/check", Protocol::Eapi},
            {ApiOperation::CloudUploadAllocate, "/api/nos/token/alloc", Protocol::Weapi},
            {ApiOperation::CloudUploadInfo, "/api/upload/cloud/info/v2", Protocol::Eapi},
            {ApiOperation::CloudPublish, "/api/cloud/pub/v2", Protocol::Eapi}};
        for (const auto& [op, path, protocol] : expected) {
            const auto c = operationContract(op);
            QVERIFY(c);
            QCOMPARE(c->logicalPath, path);
            QCOMPARE(c->protocol, protocol);
            QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
            QVERIFY(requiresAccountCookie(op));
        }
    }

    void deleteAndMatchRequests()
    {
        const auto del = collectionRequest(
            {QUuid::createUuid(), CloudSongDelete{{{"9007199254740993"}, {"2"}}}});
        QVERIFY(del);
        QCOMPARE(del->operation, ApiOperation::CloudDelete);
        const auto delPayload = operationPayload(del->operation, del->parameters);
        QVERIFY(delPayload);
        QCOMPARE(*delPayload, (QJsonObject{{"songIds", "[9007199254740993,2]"}}));
        QVERIFY(!operationPayload(ApiOperation::CloudDelete, {{"songIds", QJsonArray{}}}));
        QVERIFY(!operationPayload(ApiOperation::CloudDelete, {{"songIds", QJsonArray{1}}}));
        QVERIFY(!operationPayload(ApiOperation::CloudDelete, {{"songIds", QJsonArray{"1", "1"}}}));
        QVERIFY(!operationPayload(ApiOperation::CloudDelete,
                                  {{"songIds", QJsonArray{"1"}}, {"extra", "1"}}));

        const auto match = collectionRequest(
            {QUuid::createUuid(), CloudSongMatch{"101", {"5"}, SongId{"186016"}}});
        QVERIFY(match);
        QCOMPARE(match->operation, ApiOperation::CloudMatch);
        QCOMPARE(*operationPayload(match->operation, match->parameters),
                 (QJsonObject{{"userId", "101"}, {"songId", "5"}, {"adjustSongId", "186016"}}));
        const auto unmatch =
            collectionRequest({QUuid::createUuid(), CloudSongMatch{"101", {"5"}, std::nullopt}});
        QCOMPARE(operationPayload(unmatch->operation, unmatch->parameters)->value("adjustSongId"),
                 QJsonValue("0"));
        QVERIFY(!operationPayload(ApiOperation::CloudMatch,
                                  {{"userId", "101"}, {"songId", "5"}, {"adjustSongId", 7}}));
        QVERIFY(!operationPayload(ApiOperation::CloudMatch, {{"userId", "101"}, {"songId", "5"}}));
    }

    void uploadPayloads()
    {
        const auto check =
            operationPayload(ApiOperation::CloudUploadCheck,
                             {{"md5", md5}, {"length", "1048576"}, {"bitrate", "320000"}});
        QVERIFY(check);
        QCOMPARE(*check, (QJsonObject{{"bitrate", "320000"},
                                      {"ext", ""},
                                      {"length", 1048576},
                                      {"md5", md5},
                                      {"songId", "0"},
                                      {"version", 1}}));
        QVERIFY(!operationPayload(ApiOperation::CloudUploadCheck,
                                  {{"md5", md5.toUpper()}, {"length", "1"}, {"bitrate", "1"}}));
        QVERIFY(!operationPayload(ApiOperation::CloudUploadCheck,
                                  {{"md5", md5}, {"length", "1234567890123"}, {"bitrate", "1"}}));

        const auto allocate =
            operationPayload(ApiOperation::CloudUploadAllocate,
                             {{"md5", md5}, {"ext", "flac"}, {"filename", "晴天"}});
        QVERIFY(allocate);
        QCOMPARE(allocate->value("bucket"), QJsonValue("jd-musicrep-privatecloud-audio-public"));
        QCOMPARE(allocate->value("type"), QJsonValue("audio"));
        QCOMPARE(allocate->value("nos_product"), QJsonValue(3));
        QCOMPARE(allocate->value("local"), QJsonValue(false));
        QCOMPARE(allocate->size(), 7);
        for (const auto& ext : {"FLAC", "fl.ac", "", "abcdefghi"})
            QVERIFY(!operationPayload(ApiOperation::CloudUploadAllocate,
                                      {{"md5", md5}, {"ext", ext}, {"filename", "a"}}));
        QVERIFY(!operationPayload(ApiOperation::CloudUploadAllocate,
                                  {{"md5", md5}, {"ext", "mp3"}, {"filename", "a\nb"}}));
        QVERIFY(!operationPayload(ApiOperation::CloudUploadAllocate,
                                  {{"md5", md5}, {"ext", "mp3"}, {"filename", "  "}}));

        const QJsonObject info{{"md5", md5},
                               {"songid", "9007199254740993"},
                               {"filename", "晴天.flac"},
                               {"song", "晴天"},
                               {"album", "叶惠美"},
                               {"artist", "周杰伦"},
                               {"bitrate", "999000"},
                               {"resourceId", "42"}};
        QCOMPARE(*operationPayload(ApiOperation::CloudUploadInfo, info), info);
        auto wrong = info;
        wrong.remove("album");
        QVERIFY(!operationPayload(ApiOperation::CloudUploadInfo, wrong));
        wrong = info;
        wrong["song"] = QString(257, u'a');
        QVERIFY(!operationPayload(ApiOperation::CloudUploadInfo, wrong));

        QCOMPARE(*operationPayload(ApiOperation::CloudPublish, {{"songid", "5"}}),
                 (QJsonObject{{"songid", "5"}}));
        QVERIFY(!operationPayload(ApiOperation::CloudPublish, {{"songid", "5"}, {"x", "1"}}));
    }
};

QTEST_GUILESS_MAIN(CloudWritesTest)
#include "cloud_writes_test.moc"
