// Cloud drive writes (docs/api/cloud-drive.md): delete, match, and the upload steps' payloads.
#include "../support/synthetic_cover.h"
#include "domain/music/collection_types.h"
#include "infrastructure/netease/api/api_operation.h"
#include "infrastructure/netease/music/cloud_upload.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;

namespace
{
const QString md5 = QStringLiteral("0123456789abcdef0123456789abcdef");
// The check's songId is an opaque token (128 characters in the real reply).
const QByteArray check =
    R"({"code":200,"needUpload":true,"songId":")" + QByteArray(128, 'a') + "\"}";
const QByteArray allocation =
    R"({"code":200,"result":{"objectKey":"synthetic/key.flac","token":"UPLOAD synthetic:nos=","resourceId":42}})";
const QByteArray info = R"({"code":200,"songId":"88"})";
const QByteArray publish = R"({"code":200,"privateCloud":{}})";

struct Harness {
    QTemporaryDir dir;
    QString path;
    QByteArray content = QByteArray(300 * 1024, 'a') + "synthetic audio";
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SyntheticImageManager* nos = nullptr;
    SessionApiClient client;
    CloudUploadGateway gateway;
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto manager = std::make_unique<FakeManager>();
        out = manager.get();
        out->plans = plans;
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    static std::unique_ptr<QNetworkAccessManager> storage(QList<Plan> plans,
                                                          SyntheticImageManager*& out)
    {
        auto manager = std::make_unique<SyntheticImageManager>();
        out = manager.get();
        out->plans = plans;
        return manager;
    }
    Harness(QList<Plan> plans = {Plan{check}, Plan{allocation}, Plan{info}, Plan{publish}},
            QList<Plan> uploads = {Plan{R"({"requestID":"synthetic","offset":1})"}},
            bool auth = true)
        : client(transport(plans, http), "synthetic-cloud"),
          gateway(
              session, client,
              [](const QString&, CloudFileFacts& facts) {
                  facts.title = QStringLiteral("晴天");
                  facts.bitrate = 320000;
              },
              storage(uploads, nos))
    {
        path = dir.filePath(QStringLiteral("My Song.v2.flac"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
        f.close();
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        auto prepared = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
    quint64 submit() { return gateway.submit(session.snapshot().token, "101", {path}); }
};
CloudUploadResult result(const QSignalSpy& done)
{
    return qvariant_cast<CloudUploadResult>(done.last()[2]);
}
} // namespace

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
        auto token = info;
        token["songid"] = QString(128, u'a');
        QVERIFY(operationPayload(ApiOperation::CloudUploadInfo, token));
        token["songid"] = "a b";
        QVERIFY(!operationPayload(ApiOperation::CloudUploadInfo, token));
        wrong = info;
        wrong["song"] = QString(257, u'a');
        QVERIFY(!operationPayload(ApiOperation::CloudUploadInfo, wrong));

        QCOMPARE(*operationPayload(ApiOperation::CloudPublish, {{"songid", "5"}}),
                 (QJsonObject{{"songid", "5"}}));
        QVERIFY(!operationPayload(ApiOperation::CloudPublish, {{"songid", "5"}, {"x", "1"}}));
    }
    void readsFiles()
    {
        QTemporaryDir dir;
        const auto write = [&dir](const QString& name, const QByteArray& bytes) {
            QFile f(dir.filePath(name));
            if (f.open(QIODevice::WriteOnly))
                f.write(bytes);
            return f.fileName();
        };
        std::atomic_bool canceled = false;
        const QByteArray bytes(3 * 1024 * 1024 + 7, 'x');
        const auto facts = readCloudFile(write("a b.MP3", bytes), canceled);
        QVERIFY(facts);
        QCOMPARE(facts->size, bytes.size());
        QCOMPARE(facts->md5, QString::fromLatin1(
                                 QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex()));
        QCOMPARE(facts->extension, "mp3");
        QCOMPARE(facts->fileName, "a b.MP3");
        QVERIFY(!readCloudFile(write("empty.mp3", {}), canceled));
        QVERIFY(!readCloudFile(write("noextension", "x"), canceled));
        QVERIFY(!readCloudFile(write("bad.mp-3", "x"), canceled));
        QVERIFY(!readCloudFile(QStringLiteral("relative.mp3"), canceled));
        QVERIFY(!readCloudFile(dir.filePath("missing.mp3"), canceled));
        canceled = true;
        QVERIFY(!readCloudFile(write("c.mp3", "x"), canceled));
        QCOMPARE(cloudAllocationName("My Song.v2.flac"), "MySong_v2");
        QCOMPARE(cloudAllocationName(" .flac"), "audio");
    }

    void uploads()
    {
        Harness h;
        QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
        QSignalSpy progress(&h.gateway, &CloudUploadGateway::progressed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        const auto r = result(done);
        QCOMPARE(r.outcome, CloudUploadOutcome::Uploaded);
        QCOMPARE(r.error, CloudUploadError::None);
        QCOMPARE(r.song->value, "88");
        QVERIFY(r.transferred);
        QCOMPARE(h.nos->requests.size(), 1);
        const auto& nos = h.nos->requests.first();
        QCOMPARE(nos.url().scheme(), "https");
        QCOMPARE(nos.url().host(), "nosup-jd1.127.net");
        QVERIFY(nos.url().toEncoded().contains(
            "/jd-musicrep-privatecloud-audio-public/synthetic%2Fkey.flac?offset=0&complete=true"));
        QCOMPARE(nos.rawHeader("x-nos-token"), "UPLOAD synthetic:nos=");
        QCOMPARE(nos.rawHeader("Content-MD5"),
                 QCryptographicHash::hash(h.content, QCryptographicHash::Md5).toHex());
        QCOMPARE(nos.header(QNetworkRequest::ContentTypeHeader).toString(), "audio/flac");
        QCOMPARE(h.nos->bodies.first(), h.content);
        // The API steps after the account's cookie seed: check, allocate, info, publish.
        QCOMPARE(h.http->requests.size(), 5);
        QStringList steps;
        for (int i = 0; i < progress.size(); ++i) {
            const auto p = qvariant_cast<CloudUploadProgress>(progress[i][1]);
            if (steps.isEmpty() || steps.last() != cirrus::log::nameOf(p.step))
                steps << cirrus::log::nameOf(p.step);
        }
        QCOMPARE(steps, (QStringList{"Reading", "Checking", "Allocating", "Transferring",
                                     "Submitting", "Publishing"}));
    }

    void alreadyThere()
    {
        Harness h({Plan{R"({"code":200,"needUpload":false,"songId":"77"})"}, Plan{allocation},
                   Plan{info}, Plan{publish}});
        QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CloudUploadOutcome::Uploaded);
        QVERIFY(!result(done).transferred);
        QVERIFY(h.nos->requests.isEmpty());
    }

    void alreadyInCloud()
    {
        Harness h({Plan{R"({"code":200,"needUpload":false,"songId":"77"})"}, Plan{allocation},
                   Plan{info}, Plan{R"({"code":201})"}});
        QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CloudUploadOutcome::Uploaded);
        QVERIFY(result(done).alreadyThere);
        QCOMPARE(result(done).song->value, "88");
    }

    void failures()
    {
        {
            Harness h({Plan{R"({"code":509,"msg":"synthetic"})"}});
            QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
            QVERIFY(h.submit());
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(result(done).outcome, CloudUploadOutcome::Failed);
            QCOMPARE(result(done).error, CloudUploadError::ProviderRejected);
            QCOMPARE(result(done).providerCode, std::optional<int>(509));
            QCOMPARE(result(done).step, CloudUploadStep::Checking);
        }
        {
            Harness h(
                {Plan{check},
                 Plan{R"({"code":200,"result":{"objectKey":"../x","token":"t","resourceId":1}})"}});
            QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
            QVERIFY(h.submit());
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(result(done).error, CloudUploadError::InvalidResponse);
            QVERIFY(h.nos->requests.isEmpty());
        }
        {
            Harness h({Plan{check}, Plan{allocation}}, {Plan{"denied", 500}});
            QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
            QVERIFY(h.submit());
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(result(done).outcome, CloudUploadOutcome::Failed);
            QCOMPARE(result(done).error, CloudUploadError::Network);
            QCOMPARE(result(done).step, CloudUploadStep::Transferring);
        }
        {
            Harness h({}, {}, false);
            QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
            QVERIFY(h.submit());
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(result(done).outcome, CloudUploadOutcome::NotSent);
            QCOMPARE(result(done).error, CloudUploadError::AuthenticationRequired);
        }
    }

    void cancelsTransfer()
    {
        Plan stall{"never"};
        stall.delayMs = -1;
        Harness h({Plan{check}, Plan{allocation}}, {stall});
        QSignalSpy done(&h.gateway, &CloudUploadGateway::completed);
        QSignalSpy progress(&h.gateway, &CloudUploadGateway::progressed);
        const auto id = h.submit();
        QTRY_VERIFY(h.nos->requests.size() == 1);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, CloudUploadError::Canceled);
        QCOMPARE(result(done).outcome, CloudUploadOutcome::Failed);
    }
};

QTEST_GUILESS_MAIN(CloudWritesTest)
#include "cloud_writes_test.moc"
