#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/api/collection_operation.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
constexpr auto wide = "9007199254740993";
PlaylistDetails fullSongs()
{
    PlaylistDetails p;
    p.id = {"2"};
    p.trackIds = QList<SongId>{{wide}, {"4"}};
    p.trackCount = 2;
    p.trackIdsComplete = true;
    return p;
}
MyPlaylistsPage fullLibrary(QString account = "101")
{
    MyPlaylistsPage p;
    p.query = {account, {100, 0}};
    p.total = 2;
    p.hasMore = false;
    LibraryPlaylist a, b;
    a.id = {wide};
    b.id = {"4"};
    p.playlists = {a, b};
    return p;
}
CollectionMutation mutation(int mode, SessionToken token, QString account = "101")
{
    if (mode == 0)
        return {QUuid::createUuid(), PlaylistDelete{{wide}}};
    if (mode == 1)
        return {QUuid::createUuid(),
                *PlaylistSongOrder::fromDetails(token, fullSongs(), {{"4"}, {wide}})};
    return {QUuid::createUuid(),
            *AccountPlaylistOrder::fromPage(token, fullLibrary(account), {{"4"}, {wide}})};
}
const char* names[] = {"delete", "song-order", "account-order"};
const char* paths[] = {"/api/playlist/remove", "/api/playlist/manipulate/tracks",
                       "/api/playlist/order/update"};
const ApiOperation operations[] = {ApiOperation::PlaylistDelete, ApiOperation::PlaylistSongOrder,
                                   ApiOperation::AccountPlaylistOrder};
QJsonObject expected(int mode)
{
    if (mode == 0)
        return {{"ids", "[9007199254740993]"}};
    if (mode == 1)
        return {{"pid", "2"}, {"trackIds", "[4,9007199254740993]"}, {"op", "update"}};
    return {{"ids", "[4,9007199254740993]"}};
}
CollectionResult result(const QSignalSpy& done)
{
    return qvariant_cast<CollectionResult>(done.last()[2]);
}
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    CollectionGateway gateway{session, client};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& http)
    {
        auto manager = std::make_unique<FakeManager>();
        http = manager.get();
        http->plans = plans;
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    Harness(QList<Plan> plans, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-order")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto t = session.beginAuthentication();
        const auto p = session.prepareAuthentication(t, "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
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
    quint64 submit(int mode)
    {
        return gateway.submit(session.snapshot().token, "101",
                              mutation(mode, session.snapshot().token));
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
class PlaylistOrderTest : public QObject
{
    Q_OBJECT
  private slots:
    void factories()
    {
        const SessionToken token{QUuid::createUuid(), 1};
        auto p = fullSongs();
        const QList<SongId> after{{"4"}, {wide}};
        QVERIFY(PlaylistSongOrder::fromDetails(token, p, after));
        QVERIFY(!PlaylistSongOrder::fromDetails({}, p, after));
        p.trackIdsComplete = false;
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, after));
        p = fullSongs();
        p.trackCount.reset();
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, after));
        p.trackCount = 3;
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, after));
        p = fullSongs();
        p.trackIds.reset();
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, after));
        p = fullSongs();
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, {{wide}}));
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, {{"5"}, {wide}}));
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, {{wide}, {wide}}));
        p.trackIds = QList<SongId>{{wide}, {wide}};
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, after));
        p = fullSongs();
        p.id = {"0"};
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, after));
        auto lib = fullLibrary();
        const QList<PlaylistId> order{{"4"}, {wide}};
        QVERIFY(AccountPlaylistOrder::fromPage(token, lib, order));
        QVERIFY(!AccountPlaylistOrder::fromPage({}, lib, order));
        lib.total.reset();
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        lib = fullLibrary();
        lib.hasMore.reset();
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        lib.hasMore = true;
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        lib = fullLibrary();
        lib.total = 3;
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        lib = fullLibrary();
        lib.query.page.offset = 1;
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        lib = fullLibrary();
        lib.query.page.limit = 1;
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        lib = fullLibrary();
        lib.query.accountId = "01";
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        lib = fullLibrary();
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, {{wide}}));
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, {{wide}, {"5"}}));
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, {{wide}, {wide}}));
        lib.playlists[1].id = {wide};
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, order));
        QVERIFY(!PlaylistDelete{{"1,2"}}.isValid());
    }
    void budgets()
    {
        const SessionToken token{QUuid::createUuid(), 1};
        auto p = fullSongs();
        p.trackIds = QList<SongId>{};
        for (int i = 1; i <= 1000; ++i)
            p.trackIds->append({QString::number(i)});
        p.trackCount = 1000;
        QVERIFY(PlaylistSongOrder::fromDetails(token, p, *p.trackIds));
        p.trackIds->append({"1001"});
        p.trackCount = 1001;
        QVERIFY(!PlaylistSongOrder::fromDetails(token, p, *p.trackIds));
        auto lib = fullLibrary();
        lib.playlists.clear();
        QList<PlaylistId> ordered;
        for (int i = 1; i <= 100; ++i) {
            LibraryPlaylist row;
            row.id = {QString::number(i)};
            lib.playlists.append(row);
            ordered.append(row.id);
        }
        lib.total = 100;
        QVERIFY(AccountPlaylistOrder::fromPage(token, lib, ordered));
        LibraryPlaylist extra;
        extra.id = {"101"};
        lib.playlists.append(extra);
        ordered.append(extra.id);
        lib.total = 101;
        QVERIFY(!AccountPlaylistOrder::fromPage(token, lib, ordered));
    }
    void contracts_data()
    {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < 3; ++i)
            QTest::newRow(names[i]) << i;
    }
    void contracts()
    {
        QFETCH(int, mode);
        const auto m = mutation(mode, {QUuid::createUuid(), 1});
        const auto r = collectionRequest(m);
        QVERIFY(r);
        QCOMPARE(r->operation, operations[mode]);
        QVERIFY(isCollectionOperation(r->operation));
        const auto c = operationContract(r->operation);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, paths[mode]);
        QCOMPARE(c->protocol, mode == 1 ? Protocol::Eapi : Protocol::Weapi);
        QCOMPARE(c->responseLimit, 64 * 1024);
        QCOMPARE(c->destination, OfficialDestination::Default);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QVERIFY(requiresAccountCookie(r->operation));
        DecodedResponse d;
        d.businessCode = 200;
        QVERIFY(!acceptsResponseCookies(r->operation, d));
        QCOMPARE(operationPayload(r->operation, r->parameters),
                 std::optional<QJsonObject>(expected(mode)));
        for (const char* key : {"uid", "crypto", "checkToken", "op", "imme", "header", "e_r", "url",
                                "timestamp", "complete"}) {
            auto bad = r->parameters;
            bad.insert(key, "override");
            QVERIFY(!operationPayload(r->operation, bad));
        }
        auto bad = r->parameters;
        bad[mode == 0 ? "id" : "ids"] = true;
        QVERIFY(!operationPayload(r->operation, bad));
        if (mode) {
            for (const QJsonValue v :
                 {QJsonValue(QJsonArray{}), QJsonValue(QJsonArray{"1", "1"}),
                  QJsonValue(QJsonArray{1}), QJsonValue(QJsonArray{"01"}), QJsonValue("[1,2]")}) {
                bad = r->parameters;
                bad["ids"] = v;
                QVERIFY(!operationPayload(r->operation, bad));
            }
            QJsonArray ids;
            const int maximum = mode == 1 ? 1000 : 100;
            for (int i = 1; i <= maximum; ++i)
                ids.append(QString::number(i));
            bad = r->parameters;
            bad["ids"] = ids;
            QVERIFY(operationPayload(r->operation, bad));
            ids.append("1001");
            bad["ids"] = ids;
            QVERIFY(!operationPayload(r->operation, bad));
        }
    }
    void native_data() { contracts_data(); }
    void native()
    {
        QFETCH(int, mode);
        Plan p{R"({"code":200})"};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({p, Plan{R"({"code":200})"}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        for (int i = 0; i < 2; ++i) {
            QVERIFY(h.submit(mode));
            QTRY_COMPARE(done.size(), i + 1);
            QCOMPARE(result(done).outcome, CollectionOutcome::Acknowledged);
            QVERIFY(!result(done).createdPlaylist);
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            const auto& r = h.http->requests.last();
            QCOMPARE(r.url().path(),
                     QString(mode == 1 ? "/eapi" : "/weapi") + QString(paths[mode]).mid(4));
            QVERIFY(r.rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
            QVERIFY(!r.rawHeader("Cookie").contains("synthetic-rotation"));
            if (mode == 1) {
                const auto w = decodeEapiRequest(h.http->bodies.last());
                QVERIFY(w);
                QCOMPARE(w->logicalPath, paths[mode]);
                auto payload = w->payload;
                QVERIFY(payload.take("header").isObject());
                QCOMPARE(payload.take("e_r"), QJsonValue(false));
                QCOMPARE(payload, expected(mode));
            } else {
                QVERIFY(h.http->bodies.last().startsWith("params="));
                QVERIFY(h.http->bodies.last().contains("&encSecKey="));
            }
            QCOMPARE(h.http->requests.size(), i + 2);
        }
    }
    void responses_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<CollectionOutcome>("outcome");
        QTest::addColumn<CollectionError>("error");
        for (int i = 0; i < 3; ++i) {
            const auto pre = QByteArray(names[i]) + "-";
            for (const QByteArray b :
                 {QByteArray(R"({"code":200})"), QByteArray(R"({"code":"200"})")})
                QTest::newRow(pre + b)
                    << i << b << 200 << CollectionOutcome::Acknowledged << CollectionError::None;
            for (int code : {201, 403, 512, -462})
                QTest::newRow(pre + QByteArray::number(code))
                    << i << QByteArray("{\"code\":" + QByteArray::number(code) + "}") << 200
                    << CollectionOutcome::Rejected << CollectionError::ProviderRejected;
            for (const QByteArray b :
                 {QByteArray("{}"), QByteArray("broken"), QByteArray("[]"),
                  QByteArray(R"({"code":true})"), QByteArray(R"({"code":"2e2"})")})
                QTest::newRow(pre + b) << i << b << 200 << CollectionOutcome::Unknown
                                       << CollectionError::InvalidResponse;
            QTest::newRow(pre + "provider429")
                << i << QByteArray(R"({"code":429})") << 200 << CollectionOutcome::Rejected
                << CollectionError::RateLimited;
            QTest::newRow(pre + "http429")
                << i << QByteArray("{}") << 429 << CollectionOutcome::Unknown
                << CollectionError::RateLimited;
            QTest::newRow(pre + "http502")
                << i << QByteArray(R"({"code":200})") << 502 << CollectionOutcome::Unknown
                << CollectionError::Network;
        }
    }
    void responses()
    {
        QFETCH(int, mode);
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(CollectionOutcome, outcome);
        QFETCH(CollectionError, error);
        Harness h({Plan{body, status}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit(mode));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).error, error);
        QVERIFY(!result(done).createdPlaylist);
        if (error == CollectionError::RateLimited)
            QCOMPARE(result(done).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(1);
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(done.size(), 1);
    }
    void gates_data() { contracts_data(); }
    void gates()
    {
        QFETCH(int, mode);
        Harness guest({}, false);
        QSignalSpy g(&guest.gateway, &ICollectionGateway::completed);
        QVERIFY(guest.submit(mode));
        QTRY_COMPARE(g.size(), 1);
        QCOMPARE(result(g).outcome, CollectionOutcome::NotSent);
        QCOMPARE(result(g).error, CollectionError::AuthenticationRequired);
        QVERIFY(guest.http->requests.isEmpty());
        Harness noCookie({}, true, false);
        noCookie.client.setAnonymousToken("synthetic");
        QSignalSpy a(&noCookie.gateway, &ICollectionGateway::completed);
        QVERIFY(noCookie.submit(mode));
        QTRY_COMPARE(a.size(), 1);
        QCOMPARE(result(a).error, CollectionError::AuthenticationRequired);
        QVERIFY(noCookie.http->requests.isEmpty());
        Harness h({});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.gateway.submit(token, "202", mutation(mode, token)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, CollectionError::InvalidRequest);
        if (mode) {
            QVERIFY(h.gateway.submit(token, "101", mutation(mode, {QUuid::createUuid(), 1})));
            QTRY_COMPARE(done.size(), 2);
            QCOMPARE(result(done).error, CollectionError::StaleContext);
        }
        if (mode == 2) {
            QVERIFY(h.gateway.submit(token, "101", mutation(mode, token, "202")));
            QTRY_COMPARE(done.size(), 3);
            QCOMPARE(result(done).error, CollectionError::InvalidRequest);
        }
        QCOMPARE(h.http->requests.size(), 1);
    }
    void cancel_data() { contracts_data(); }
    void cancel()
    {
        QFETCH(int, mode);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto id = h.submit(mode);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QCOMPARE(h.http->requests.size(), 1);
        const auto sent = h.submit(mode);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.gateway.cancel(sent);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        const auto old = h.session.snapshot().token;
        QVERIFY(h.submit(mode));
        QTRY_COMPARE(h.http->requests.size(), 3);
        QVERIFY(h.session.beginAuthentication().isValid());
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), old);
    }
    void facade()
    {
        BackendFacade f;
        ready(f);
        const auto token = f.snapshot().token;
        QSignalSpy requested(&f, &BackendFacade::collectionRequested),
            done(&f, &BackendFacade::collectionCompleted);
        QVERIFY(!f.mutateCollection(mutation(1, {QUuid::createUuid(), 1})));
        QVERIFY(!f.mutateCollection(mutation(2, token, "202")));
        QVERIFY(requested.isEmpty());
        for (int i = 0; i < 8; ++i)
            QVERIFY(f.mutateCollection(mutation(i % 3, token)));
        QCOMPARE(f.mutateCollection(mutation(0, token)), quint64(0));
        const auto c = qvariant_cast<CollectionCommand>(requested.first()[0]);
        QCOMPARE(f.mutateCollection(c.mutation), quint64(0));
        f.cancelCollection(c.id);
        QTRY_COMPARE(done.size(), 1);
        CollectionResult late;
        late.outcome = CollectionOutcome::Acknowledged;
        late.createdPlaylist = PlaylistId{"7"};
        f.deliverCollection({c.id, c.generation, c.context, late});
        QCOMPARE(done.size(), 1);
        QVERIFY(!result(done).createdPlaylist);
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 8);
        for (const auto& row : done) {
            QCOMPARE(qvariant_cast<SessionToken>(row[1]), token);
            QCOMPARE(qvariant_cast<CollectionResult>(row[2]).outcome, CollectionOutcome::Unknown);
        }
    }
};
QTEST_GUILESS_MAIN(PlaylistOrderTest)
#include "playlist_order_test.moc"
