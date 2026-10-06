#include "../support/synthetic_cover.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/api/api_operation.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
const QByteArray allocation =
    R"({"code":200,"result":{"objectKey":"synthetic/key","token":"synthetic-nos","docId":"9007199254740995"}})";
CollectionMutation mutation()
{
    return {QUuid::createUuid(), PlaylistCoverReplace{{"9007199254740993"}, syntheticCover()}};
}
CollectionResult result(const QSignalSpy& done)
{
    return qvariant_cast<CollectionResult>(done.last()[2]);
}
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SyntheticImageManager* nos = nullptr;
    SessionApiClient client;
    CollectionGateway gateway;
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto manager = std::make_unique<FakeManager>();
        out = manager.get();
        out->plans = plans;
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    static std::unique_ptr<CoverUpload> upload(QList<Plan> plans, SyntheticImageManager*& out)
    {
        auto manager = std::make_unique<SyntheticImageManager>();
        out = manager.get();
        out->plans = plans;
        return std::make_unique<CoverUpload>(std::move(manager));
    }
    Harness(QList<Plan> plans = {Plan{allocation}, Plan{R"({"code":200})"}},
            QList<Plan> images = {Plan{"auto"}}, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-cover"),
          gateway(session, client, upload(images, nos))
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        auto prepared = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        if (!cookie)
            return;
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
    quint64 submit(CollectionMutation m = mutation())
    {
        return gateway.submit(session.snapshot().token, "101", m);
    }
};
void ready(BackendFacade& f)
{
    f.start();
    BackendEvent e;
    e.kind = BackendEvent::Kind::Started;
    f.deliver(e);
    e.kind = BackendEvent::Kind::Session;
    e.session = {{QUuid::createUuid(), 1}, SessionPhase::Authenticated, "101"};
    f.deliver(e);
}
} // namespace
class PlaylistCoverWritesTest : public QObject
{
    Q_OBJECT
  private slots:
    void imageAndClosedContracts()
    {
        const auto input = syntheticCover();
        QVERIFY(!input.isEmpty());
        const auto clean = prepareCoverJpeg(input);
        QVERIFY(!clean.isEmpty());
        QImage decoded;
        QVERIFY(decoded.loadFromData(clean, "JPEG"));
        QCOMPARE(decoded.size(), QSize(8, 8));
        QVERIFY(decoded.textKeys().isEmpty());
        QVERIFY(prepareCoverJpeg(syntheticCover(1024, 1024)).size() > 0);
        QVERIFY(prepareCoverJpeg(syntheticCover(1025, 1025)).isEmpty());
        QVERIFY(prepareCoverJpeg(syntheticCover(8, 9)).isEmpty());
        QVERIFY(prepareCoverJpeg(QByteArray::fromHex("ffd8ff00ffd9")).isEmpty());
        QVERIFY(prepareCoverJpeg(input.left(input.size() - 2)).isEmpty());
        auto large = input;
        large.insert(3, QByteArray(PlaylistCoverReplace::MaximumBytes, 'x'));
        QVERIFY(!PlaylistCoverReplace({{"1"}, large}).isValid());
        QVERIFY(!PlaylistCoverReplace({{"01"}, input}).isValid());
        for (auto op : {ApiOperation::CoverAllocate, ApiOperation::PlaylistCoverCommit}) {
            const auto c = operationContract(op);
            QVERIFY(c);
            QCOMPARE(c->protocol, Protocol::Weapi);
            QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(c->responseLimit, 64 * 1024);
            QVERIFY(requiresAccountCookie(op));
            QCOMPARE(c->logicalPath, op == ApiOperation::CoverAllocate
                                         ? "/api/nos/token/alloc"
                                         : "/api/playlist/cover/update");
        }
        const auto p = operationPayload(ApiOperation::CoverAllocate, {});
        QVERIFY(p);
        QCOMPARE(p->size(), 7);
        QCOMPARE(p->value("bucket"), QJsonValue("yyimgs"));
        QCOMPARE(p->value("ext"), QJsonValue("jpg"));
        QCOMPARE(p->value("filename"), QJsonValue("cirrus-cover.jpg"));
        QCOMPARE(p->value("local"), QJsonValue(false));
        QCOMPARE(p->value("nos_product"), QJsonValue(0));
        QCOMPARE(p->value("type"), QJsonValue("other"));
        QCOMPARE(p->value("return_body"), QJsonValue("{\"code\":200,\"size\":\"$(ObjectSize)\"}"));
        const QJsonObject commit{{"id", "9007199254740993"}, {"coverImgId", "9007199254740995"}};
        QCOMPARE(*operationPayload(ApiOperation::PlaylistCoverCommit, commit), commit);
        for (QString key :
             {"header", "checkToken", "filename", "bucket", "url", "imgX", "crypto"}) {
            QVERIFY(!operationPayload(ApiOperation::CoverAllocate, {{key, "synthetic"}}));
            auto wrong = commit;
            wrong[key] = "synthetic";
            QVERIFY(!operationPayload(ApiOperation::PlaylistCoverCommit, wrong));
        }
        auto wrong = commit;
        wrong["coverImgId"] = 1;
        QVERIFY(!operationPayload(ApiOperation::PlaylistCoverCommit, wrong));
    }
    void badAllocation_data()
    {
        QTest::addColumn<QByteArray>("body");
        const QJsonObject valid{{"objectKey", "synthetic/key"},
                                {"token", "synthetic-nos"},
                                {"docId", "9007199254740995"}};
        int row = 0;
        for (QString field : {"objectKey", "token", "docId"}) {
            for (QJsonValue value :
                 {QJsonValue(), QJsonValue(0), QJsonValue(""), QJsonValue(QJsonArray{})}) {
                auto o = valid;
                o[field] = value;
                QTest::newRow(qPrintable(QString::number(++row)))
                    << QJsonDocument(QJsonObject{{"code", 200}, {"result", o}})
                           .toJson(QJsonDocument::Compact);
            }
        }
        for (const auto& key :
             QStringList{"../key", "x/../key", "x//key", "/key", "key/", "https://bad", "x%2fkey",
                         "x?y", "x#y", "x\\y", "x\ny", "key\n", QString(513, 'x')}) {
            auto o = valid;
            o["objectKey"] = key;
            QTest::newRow(qPrintable(QString::number(++row)))
                << QJsonDocument(QJsonObject{{"code", 200}, {"result", o}})
                       .toJson(QJsonDocument::Compact);
        }
        for (const auto& token : QStringList{"x y", "x\ny", "é", QString(4097, 'x')}) {
            auto o = valid;
            o["token"] = token;
            QTest::newRow(qPrintable(QString::number(++row)))
                << QJsonDocument(QJsonObject{{"code", 200}, {"result", o}})
                       .toJson(QJsonDocument::Compact);
        }
        QTest::newRow("missing-result") << QByteArray(R"({"code":200})");
        QTest::newRow("numeric-wide") << QByteArray(
            R"({"code":200,"result":{"objectKey":"synthetic","token":"synthetic","docId":9007199254740993}})");
    }
    void badAllocation()
    {
        QFETCH(QByteArray, body);
        Harness h({Plan{body}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::InvalidResponse);
        QCOMPARE(result(done).coverStep, std::optional<CoverStep>(CoverStep::Allocating));
        QCOMPARE(h.http->requests.size(), 2);
        QVERIFY(h.nos->requests.isEmpty());
    }
    void nativeChain()
    {
        Plan alloc{allocation};
        alloc.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({alloc, Plan{R"({"code":200,"playlist":{"id":"7"}})"}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        const auto m = mutation();
        const auto id = h.submit(m);
        QVERIFY(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::Acknowledged);
        QCOMPARE(result(done).coverStep, std::optional<CoverStep>(CoverStep::Committing));
        QCOMPARE(result(done).eventId, m.eventId);
        QVERIFY(!result(done).createdPlaylist);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QCOMPARE(h.http->requests.size(), 3);
        QCOMPARE(h.nos->requests.size(), 1);
        QCOMPARE(h.http->requests[1].url().toString(),
                 "https://music.163.com/weapi/nos/token/alloc");
        QCOMPARE(h.http->requests[2].url().path(), "/weapi/playlist/cover/update");
        for (int i : {1, 2}) {
            QVERIFY(h.http->bodies[i].startsWith("params="));
            QVERIFY(h.http->requests[i].rawHeader("Cookie").contains("synthetic-original"));
            QVERIFY(!h.http->requests[i].rawHeader("Cookie").contains("synthetic-rotation"));
        }
        const auto wire = h.nos->requests.first();
        QCOMPARE(
            wire.url().toString(),
            "https://nosup-hz1.127.net/yyimgs/synthetic/key?offset=0&complete=true&version=1.0");
        QCOMPARE(wire.rawHeader("x-nos-token"), QByteArray("synthetic-nos"));
        QCOMPARE(wire.header(QNetworkRequest::ContentTypeHeader).toString(), "image/jpeg");
        QVERIFY(wire.rawHeader("Cookie").isEmpty());
        QVERIFY(wire.rawHeader("Authorization").isEmpty());
        QVERIFY(wire.rawHeader("Referer").isEmpty());
        QCOMPARE(wire.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(wire.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
                 int(QNetworkRequest::Manual));
        QCOMPARE(h.nos->bodies.first(),
                 prepareCoverJpeg(std::get<PlaylistCoverReplace>(m.change).jpeg));
    }
    void nosFailure_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<CollectionOutcome>("outcome");
        for (const QByteArray body :
             {QByteArray("{}"), QByteArray("broken"), QByteArray(R"({"code":200,"size":"0"})"),
              QByteArray(R"({"code":200,"size":10})"), QByteArray(16385, 'x')})
            QTest::newRow(QByteArray::number(body.size()) + body.left(10))
                << body << 200 << CollectionOutcome::Unknown;
        QTest::newRow("redirect") << QByteArray("auto") << 302 << CollectionOutcome::Unknown;
        QTest::newRow("http429") << QByteArray("auto") << 429 << CollectionOutcome::Unknown;
        QTest::newRow("provider403")
            << QByteArray(R"({"code":403})") << 200 << CollectionOutcome::Rejected;
    }
    void nosFailure()
    {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(CollectionOutcome, outcome);
        Harness h({Plan{allocation}}, {Plan{body, status}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).coverStep, std::optional<CoverStep>(CoverStep::Uploading));
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(h.nos->requests.size(), 1);
        QTest::qWait(1);
        QCOMPARE(done.size(), 1);
    }
    void finalFailure_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<CollectionOutcome>("outcome");
        QTest::newRow("403") << QByteArray(R"({"code":403})") << CollectionOutcome::Rejected;
        QTest::newRow("512") << QByteArray(R"({"code":512})") << CollectionOutcome::Rejected;
        QTest::newRow("missing-code") << QByteArray("{}") << CollectionOutcome::Unknown;
        QTest::newRow("string200")
            << QByteArray(R"({"code":"200"})") << CollectionOutcome::Acknowledged;
    }
    void finalFailure()
    {
        QFETCH(QByteArray, body);
        QFETCH(CollectionOutcome, outcome);
        Harness h({Plan{allocation}, Plan{body}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).coverStep, std::optional<CoverStep>(CoverStep::Committing));
        QCOMPARE(h.http->requests.size(), 3);
        QCOMPARE(h.nos->requests.size(), 1);
    }
    void cancellation_data()
    {
        QTest::addColumn<int>("phase");
        QTest::addColumn<bool>("logout");
        for (int phase : {0, 1, 2, 3})
            for (bool logout : {false, true})
                QTest::newRow(qPrintable(QString::number(phase) + "-" + QString::number(logout)))
                    << phase << logout;
    }
    void cancellation()
    {
        QFETCH(int, phase);
        QFETCH(bool, logout);
        Harness h({phase == 1 ? Plan{"", 200, -1} : Plan{allocation}, Plan{"", 200, -1}},
                  {phase == 2 ? Plan{"", 200, -1} : Plan{"auto"}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.submit();
        QVERIFY(id);
        if (phase == 1)
            QTRY_COMPARE(h.http->requests.size(), 2);
        else if (phase == 2)
            QTRY_COMPARE(h.nos->requests.size(), 1);
        else if (phase == 3)
            QTRY_COMPARE(h.http->requests.size(), 3);
        if (logout)
            h.session.signOut();
        else
            h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome,
                 phase == 0 ? CollectionOutcome::NotSent : CollectionOutcome::Unknown);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QVERIFY(!result(done).createdPlaylist);
        const int apiCount = h.http->requests.size(), nosCount = h.nos->requests.size();
        QTest::qWait(5);
        QCOMPARE(h.http->requests.size(), apiCount);
        QCOMPARE(h.nos->requests.size(), nosCount);
        QCOMPARE(done.size(), 1);
    }
    void clientDestroyedDuringUpload()
    {
        SessionCoordinator session;
        auto prepared = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        const auto token = session.snapshot().token;
        FakeManager* http = nullptr;
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        auto client = std::make_unique<SessionApiClient>(
            Harness::transport({seed, Plan{allocation}}, http), "synthetic-cover");
        QVERIFY(client->advanceTo(token));
        QSignalSpy seeded(client.get(), &SessionApiClient::completed);
        QVERIFY(client->submit(token, ApiOperation::QrKey));
        QTRY_COMPARE(seeded.size(), 1);
        SyntheticImageManager* nos = nullptr;
        CollectionGateway gateway(session, *client, Harness::upload({Plan{"", 200, -1}}, nos));
        QSignalSpy done(&gateway, &ICollectionGateway::completed);
        const auto m = mutation();
        QVERIFY(gateway.submit(token, "101", m));
        QTRY_COMPARE(nos->requests.size(), 1);
        QCOMPARE(http->requests.size(), 2);
        client.reset(); // No pending API id here: prior allocation still forbids NotSent.
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::Network);
        QCOMPARE(result(done).coverStep, std::optional<CoverStep>(CoverStep::Uploading));
        QCOMPARE(result(done).eventId, m.eventId);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QTRY_COMPARE(nos->findChildren<QNetworkReply*>().size(), 0);
        QCOMPARE(nos->requests.size(), 1);
        QTest::qWait(5);
        QCOMPARE(done.size(), 1);
    }
    void gatesAndFacade()
    {
        Harness guest({}, {}, false);
        QSignalSpy g(&guest.gateway, &ICollectionGateway::completed);
        QVERIFY(guest.submit());
        QTRY_COMPARE(g.size(), 1);
        QCOMPARE(result(g).outcome, CollectionOutcome::NotSent);
        QVERIFY(guest.http->requests.isEmpty());
        Harness noCookie({}, {}, true, false);
        QSignalSpy c(&noCookie.gateway, &ICollectionGateway::completed);
        QVERIFY(noCookie.submit());
        QTRY_COMPARE(c.size(), 1);
        QCOMPARE(result(c).error, CollectionError::AuthenticationRequired);
        QVERIFY(noCookie.http->requests.isEmpty());
        QVERIFY(noCookie.nos->requests.isEmpty());
        Harness bad;
        QSignalSpy b(&bad.gateway, &ICollectionGateway::completed);
        auto m = mutation();
        std::get<PlaylistCoverReplace>(m.change).jpeg = QByteArray::fromHex("ffd8ff00ffd9");
        QVERIFY(bad.submit(m));
        QTRY_COMPARE(b.size(), 1);
        QCOMPARE(result(b).outcome, CollectionOutcome::NotSent);
        QCOMPARE(result(b).coverStep, std::optional<CoverStep>(CoverStep::Preparing));
        QCOMPARE(bad.http->requests.size(), 1);
        BackendFacade f;
        ready(f);
        QSignalSpy commands(&f, &BackendFacade::collectionRequested),
            done(&f, &BackendFacade::collectionCompleted),
            reads(&f, &BackendFacade::musicRequested);
        for (int i = 0; i < 8; ++i)
            QVERIFY(f.mutateCollection(mutation()));
        QVERIFY(!f.mutateCollection(mutation()));
        const auto command = qvariant_cast<CollectionCommand>(commands.first()[0]);
        QVERIFY(!f.mutateCollection(command.mutation));
        CollectionResult premature;
        premature.outcome = CollectionOutcome::Acknowledged;
        premature.coverStep = CoverStep::Uploading;
        f.deliverCollection({command.id, command.generation, command.context, premature});
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        f.close();
        QTRY_COMPARE(done.size(), 8);
        QVERIFY(reads.isEmpty());
        QCOMPARE(result(done).coverStep, std::optional<CoverStep>(CoverStep::Unobserved));
    }
};
QTEST_GUILESS_MAIN(PlaylistCoverWritesTest)
#include "playlist_cover_writes_test.moc"
