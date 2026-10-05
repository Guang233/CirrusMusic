#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/chart_mapper.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
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
QJsonObject json(const QByteArray& b) { return QJsonDocument::fromJson(b).object(); }
MusicResult mapped(const QByteArray& b) { return mapMusicResponse(ChartsQuery{}, json(b)); }
MusicResult result(const QSignalSpy& s) { return qvariant_cast<MusicResult>(s.last()[2]); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto m = std::make_unique<FakeManager>();
        out = m.get();
        m->plans = plans;
        return std::make_unique<HttpTransport>(std::move(m));
    }
    explicit Harness(QList<Plan> plans, bool auth = false)
        : client(transport(plans, http), "synthetic-charts")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        client.setAnonymousToken("synthetic-anonymous");
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto prepared = session.prepareAuthentication(session.beginAuthentication(), "101");
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
};
} // namespace
class ChartsTest : public QObject
{
    Q_OBJECT
  private slots:
    void closedContracts()
    {
        QCOMPARE(ChartsQuery{}.kind, ChartListKind::Overview);
        QVERIFY(!validMusicQuery(ChartsQuery{static_cast<ChartListKind>(2)}));
        QVERIFY(!chartOperation(ChartsQuery{static_cast<ChartListKind>(2)}));
        QVERIFY(!chartOperation(SongSearch{"synthetic"}));
        for (const auto kind : {ChartListKind::Overview, ChartListKind::WithPreviews}) {
            const MusicQuery q = ChartsQuery{kind};
            QVERIFY(validMusicQuery(q));
            QVERIFY(!requiresAuthenticatedMusicQuery(q));
            QVERIFY(!musicQueryAccount(q));
            const auto op = chartOperation(q);
            QVERIFY(op);
            QVERIFY(!requiresAccountCookie(*op));
            const auto c = operationContract(*op);
            QVERIFY(c);
            QCOMPARE(c->responseLimit, 4 * 1024 * 1024);
            QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(c->destination, OfficialDestination::Default);
            QCOMPARE(c->logicalPath,
                     kind == ChartListKind::Overview ? "/api/toplist" : "/api/toplist/detail");
            QCOMPARE(c->protocol,
                     kind == ChartListKind::Overview ? Protocol::Eapi : Protocol::Weapi);
            const auto payload = operationPayload(*op, {});
            QVERIFY(payload);
            QVERIFY(payload->isEmpty());
            QVERIFY(!operationPayload(*op, {{"uid", "101"}}));
            QVERIFY(!operationPayload(*op, {{"limit", 1}}));
            QVERIFY(!operationPayload(*op, {{"host", "https://example.invalid"}}));
            DecodedResponse response;
            response.businessCode = 200;
            response.object = {{"code", 200}};
            QVERIFY(!acceptsResponseCookies(*op, response));
        }
    }
    void metadataAndUnknowns()
    {
        const ChartsQuery q{ChartListKind::WithPreviews};
        const auto r = mapMusicResponse(
            q,
            json(
                R"({"code":200,"list":[{"id":"9007199254740993","name":"synthetic chart","description":"synthetic description","coverImgUrl":"https:\/\/example.invalid\/cover","trackCount":8,"updateTime":1730000000000,"updateFrequency":"provider label","tracks":[{"first":"synthetic preview","second":"provider credit"},{"first":"unknown credit"}]},{"id":2,"trackCount":0,"tracks":[]},{"id":"9007199254740993","tracks":null}],"artistToplist":{"artists":[{"id":999}]},"rewardToplist":{"ignored":true}})"));
        QVERIFY(std::holds_alternative<Charts>(r));
        const auto c = std::get<Charts>(r);
        QCOMPARE(c.query.kind, q.kind);
        QCOMPARE(c.charts.size(), 3);
        QCOMPARE(c.charts[0].playlist.value, c.charts[2].playlist.value);
        const auto& a = c.charts.first();
        QCOMPARE(a.title, "synthetic chart");
        QCOMPARE(a.description, "synthetic description");
        QCOMPARE(a.cover, QUrl("https://example.invalid/cover"));
        QCOMPARE(a.trackCount, std::optional<qint64>(8));
        QCOMPARE(a.providerUpdateTime, std::optional<qint64>(1730000000000LL));
        QCOMPARE(a.updateFrequency, std::optional<QString>("provider label"));
        QVERIFY(a.previews);
        QCOMPARE(a.previews->size(), 2);
        QCOMPARE(a.previews->first().primaryText, "synthetic preview");
        QCOMPARE(a.previews->first().secondaryText, std::optional<QString>("provider credit"));
        QVERIFY(!a.previews->last().secondaryText);
        QVERIFY(c.charts[1].previews);
        QVERIFY(c.charts[1].previews->isEmpty());
        QCOMPARE(c.charts[1].trackCount, std::optional<qint64>(0));
        QVERIFY(!c.charts[2].trackCount);
        QVERIFY(!c.charts[2].previews);
        QVERIFY(!c.charts[2].providerUpdateTime);
        QVERIFY(!c.charts[2].updateFrequency);
        const auto empty =
            mapped(R"({"code":200,"list":[],"artistToplist":{"artists":[{"id":1}]}})");
        QVERIFY(std::holds_alternative<Charts>(empty));
        QVERIFY(std::get<Charts>(empty).charts.isEmpty());
        const auto unsupported =
            mapped(R"({"code":200,"list":[{"id":1,"coverImgUrl":"file:\/\/\/tmp/no"}]})");
        QVERIFY(std::holds_alternative<Charts>(unsupported));
        QVERIFY(std::get<Charts>(unsupported).charts.first().cover.isEmpty());
    }
    void malformed_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("no-list") << QByteArray(R"({"code":200,"artistToplist":{"artists":[]}})");
        QTest::newRow("null-list") << QByteArray(R"({"code":200,"list":null})");
        QTest::newRow("non-object") << QByteArray(R"({"code":200,"list":[1]})");
        QTest::newRow("missing-id") << QByteArray(R"({"code":200,"list":[{"name":"synthetic"}]})");
        QTest::newRow("noncanonical-id") << QByteArray(R"({"code":200,"list":[{"id":"01"}]})");
        QTest::newRow("unsafe-numeric-id")
            << QByteArray(R"({"code":200,"list":[{"id":9007199254740993}]})");
        QTest::newRow("wrong-name") << QByteArray(R"({"code":200,"list":[{"id":1,"name":true}]})");
        QTest::newRow("wrong-description")
            << QByteArray(R"({"code":200,"list":[{"id":1,"description":[]} ]})");
        QTest::newRow("negative-count")
            << QByteArray(R"({"code":200,"list":[{"id":1,"trackCount":-1}]})");
        QTest::newRow("fractional-count")
            << QByteArray(R"({"code":200,"list":[{"id":1,"trackCount":1.5}]})");
        QTest::newRow("unsafe-update-time")
            << QByteArray(R"({"code":200,"list":[{"id":1,"updateTime":9007199254740993}]})");
        QTest::newRow("wrong-frequency")
            << QByteArray(R"({"code":200,"list":[{"id":1,"updateFrequency":true}]})");
        QTest::newRow("wrong-tracks")
            << QByteArray(R"({"code":200,"list":[{"id":1,"tracks":{}}]})");
        QTest::newRow("non-object-preview")
            << QByteArray(R"({"code":200,"list":[{"id":1,"tracks":[1]}]})");
        QTest::newRow("missing-primary")
            << QByteArray(R"({"code":200,"list":[{"id":1,"tracks":[{"second":"synthetic"}]}]})");
        QTest::newRow("wrong-secondary") << QByteArray(
            R"({"code":200,"list":[{"id":1,"tracks":[{"first":"synthetic","second":3}]}]})");
        QTest::newRow("bad-code") << QByteArray(R"({"code":500,"list":[]})");
    }
    void malformed()
    {
        QFETCH(QByteArray, body);
        const auto r = mapped(body);
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void boundedTextAndArrays()
    {
        QJsonArray rows;
        for (int i = 1; i <= Charts::MaximumCharts; ++i)
            rows.append(QJsonObject{{"id", i}});
        const auto q = ChartsQuery{};
        QVERIFY(
            std::holds_alternative<Charts>(mapMusicResponse(q, {{"code", 200}, {"list", rows}})));
        rows.append(QJsonObject{{"id", 201}});
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"list", rows}})));
        QJsonArray previews;
        for (int i = 0; i < Chart::MaximumPreviews; ++i)
            previews.append(QJsonObject{{"first", "synthetic"}});
        QJsonObject row{{"id", 1}, {"tracks", previews}};
        QVERIFY(std::holds_alternative<Charts>(
            mapMusicResponse(q, {{"code", 200}, {"list", QJsonArray{row}}})));
        previews.append(QJsonObject{{"first", "synthetic"}});
        row.insert("tracks", previews);
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"list", QJsonArray{row}}})));
        for (const auto key : {"name", "updateFrequency", "description"}) {
            row = {{"id", 1}, {key, QString(key == QString("description") ? 65537 : 4097, u'x')}};
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapMusicResponse(q, {{"code", 200}, {"list", QJsonArray{row}}})));
        }
        row = {{"id", 1}, {"name", QString(QChar::Null)}};
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"list", QJsonArray{row}}})));
        row = {{"id", 1}, {"name", QString(QChar(0xd800))}};
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"list", QJsonArray{row}}})));
    }
    void nativeReads_data()
    {
        QTest::addColumn<bool>("authenticated");
        QTest::addColumn<bool>("previews");
        QTest::newRow("guest-overview") << false << false;
        QTest::newRow("guest-previews") << false << true;
        QTest::newRow("account-overview") << true << false;
        QTest::newRow("account-previews") << true << true;
    }
    void nativeReads()
    {
        QFETCH(bool, authenticated);
        QFETCH(bool, previews);
        Plan first{
            R"({"code":200,"list":[{"id":1,"tracks":[{"first":"synthetic display only"}]}]})"};
        first.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({first, Plan{R"({"code":200,"list":[]})"}}, authenticated);
        const ChartsQuery q{previews ? ChartListKind::WithPreviews : ChartListKind::Overview};
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, q);
        QVERIFY(id);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(std::holds_alternative<Charts>(result(done)));
        QCOMPARE(done.last()[0].toULongLong(), id);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QCOMPARE(h.http->requests.last().url().path(),
                 previews ? "/weapi/toplist/detail" : "/eapi/toplist");
        QCOMPARE(h.http->requests.last().url().host(),
                 previews ? "music.163.com" : "interfacepc.music.163.com");
        if (!previews) {
            const auto wire = decodeEapiRequest(h.http->bodies.last());
            QVERIFY(wire);
            QCOMPARE(wire->logicalPath, "/api/toplist");
            auto business = wire->payload;
            business.remove("header");
            business.remove("e_r");
            QVERIFY(business.isEmpty());
        }
        QVERIFY(h.service.request(token, q));
        QTRY_COMPARE(done.size(), 2);
        const auto cookie = h.http->requests.last().rawHeader("Cookie");
        QVERIFY(!cookie.contains("synthetic-rotation"));
        if (authenticated)
            QVERIFY(cookie.contains("MUSIC_U=synthetic-original"));
        else
            QVERIFY(!cookie.contains("MUSIC_U="));
        QCOMPARE(h.http->requests.size(),
                 authenticated ? 3 : 2); // no playlist/preview hydration or report
    }
    void cancelStaleAndRateLimit()
    {
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, ChartsQuery{});
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 1);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(token, ChartsQuery{ChartListKind::WithPreviews}));
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        Harness rate({Plan{"", 429}});
        QSignalSpy r(&rate.service, &MusicQueryService::completed);
        QVERIFY(rate.service.request(rate.session.snapshot().token, ChartsQuery{}));
        QTRY_COMPARE(r.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(r)).error, MusicError::RateLimited);
        QCOMPARE(std::get<MusicFailure>(result(r)).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(rate.http->requests.size(), 1);
    }
    void facadeDefaultsMailboxAndLateResult()
    {
        BackendFacade f;
        QVERIFY(!f.charts());
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        const SessionToken token{QUuid::createUuid(), 1};
        e.kind = BackendEvent::Kind::Session;
        e.session = {token, SessionPhase::Guest};
        f.deliver(e);
        QSignalSpy commands(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted),
            reports(&f, &BackendFacade::listeningReportRequested),
            play(&f, &BackendFacade::playbackRequested);
        QVERIFY(!f.charts(static_cast<ChartListKind>(2)));
        const auto id = f.charts();
        QVERIFY(id);
        const auto c = qvariant_cast<MusicCommand>(commands.last()[0]);
        QCOMPARE(std::get<ChartsQuery>(c.query).kind, ChartListKind::Overview);
        f.deliverMusic({id, c.generation, token, Charts{std::get<ChartsQuery>(c.query), {}}});
        QCOMPARE(done.size(), 1);
        QVERIFY(f.charts(ChartListKind::WithPreviews));
        const auto preview = qvariant_cast<MusicCommand>(commands.last()[0]);
        QCOMPARE(std::get<ChartsQuery>(preview.query).kind, ChartListKind::WithPreviews);
        for (int i = 0; i < 15; ++i)
            QVERIFY(f.charts());
        QVERIFY(!f.charts());
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 17);
        f.deliverMusic({preview.id, preview.generation, token,
                        Charts{std::get<ChartsQuery>(preview.query), {}}});
        QCOMPARE(done.size(), 17);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QVERIFY(reports.isEmpty());
        QVERIFY(play.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ChartsTest)
#include "charts_test.moc"
