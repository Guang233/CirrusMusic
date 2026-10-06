#include "../support/fake_http.h"
#include "../support/synthetic_daily_playlists.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include "infrastructure/netease/music/playlist_discovery_mapper.h"
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
    Harness(QList<Plan> plans = {}, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-daily-playlists")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        client.setAnonymousToken("synthetic-anon");
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
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
class DailyPlaylistsTest : public QObject
{
    Q_OBJECT
  private slots:
    void contractAndValues()
    {
        const MusicQuery q = DailyRecommendedPlaylistsQuery{"101"};
        QVERIFY(validMusicQuery(q));
        QVERIFY(isPlaylistDiscoveryQuery(q));
        QVERIFY(requiresAuthenticatedMusicQuery(q));
        QCOMPARE(musicQueryAccount(q), std::optional<QString>("101"));
        QVERIFY(!requiresAuthenticatedMusicQuery(RecommendedPlaylistsQuery{}));
        const auto r = playlistDiscoveryRequest(q);
        QVERIFY(r);
        QCOMPARE(r->operation, ApiOperation::DailyRecommendedPlaylists);
        QVERIFY(r->parameters.isEmpty());
        const auto c = operationContract(r->operation);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, "/api/v1/discovery/recommend/resource");
        QCOMPARE(c->protocol, Protocol::Weapi);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QCOMPARE(c->responseLimit, 1024 * 1024);
        QVERIFY(requiresAccountCookie(r->operation));
        QVERIFY(operationPayload(r->operation, {}));
        QVERIFY(operationPayload(r->operation, {})->isEmpty());
        for (const auto& key : QStringList{"uid", "limit", "offset", "date", "afresh", "header",
                                           "e_r", "crypto", "checkToken", "host"})
            QVERIFY(!operationPayload(r->operation, {{key, 1}}));
        for (const auto& id :
             QStringList{"", "0", "01", "+1", "-1", "1.0", " 1", "9223372036854775808"}) {
            const MusicQuery bad = DailyRecommendedPlaylistsQuery{id};
            QVERIFY(!validMusicQuery(bad));
            QVERIFY(!playlistDiscoveryRequest(bad));
        }
        QVERIFY(validMusicQuery(DailyRecommendedPlaylistsQuery{"9007199254740993"}));
        const auto page =
            std::get<DailyRecommendedPlaylists>(mapMusicResponse(q, json(dailyPlaylistsFixture())));
        QCOMPARE(page.query.accountId, "101");
        QCOMPARE(page.playlists.size(), 3);
        QCOMPARE(page.playlists.first().id.value, "9007199254740993");
        QCOMPARE(page.playlists.last().id.value, page.playlists.first().id.value);
        QCOMPARE(page.playlists.first().title, "  <b>synthetic daily</b>  ");
        QCOMPARE(page.playlists.first().playCount, std::optional<double>(12.5));
        QCOMPARE(page.playlists.first().trackCount, std::optional<qint64>(0));
        QCOMPARE(page.playlists.first().providerUpdateTime, std::optional<qint64>(0));
        QVERIFY(!page.playlists[1].summary);
        QVERIFY(page.playlists[1].cover.isEmpty());
        QVERIFY(!page.playlists[1].playCount);
        QCOMPARE(page.playlists.last().summary, std::optional<QString>(""));
        QCOMPARE(page.playlists.last().playCount, std::optional<double>(0));
        const auto empty = mapMusicResponse(q, json(R"({"code":200,"recommend":[]})"));
        QVERIFY(std::get<DailyRecommendedPlaylists>(empty).playlists.isEmpty());
    }
    void badSchema_data()
    {
        QTest::addColumn<QJsonObject>("body");
        int n = 0;
        const auto row = [&](QJsonObject o) {
            QTest::newRow(qPrintable(QString::number(++n))) << o;
        };
        for (const auto& b : QList<QByteArray>{
                 R"({})", R"({"code":200})", R"({"code":"200","recommend":[]})",
                 R"({"code":true,"recommend":[]})", R"({"code":200,"recommend":null})",
                 R"({"code":200,"recommend":{}})", R"({"code":200,"result":[]})",
                 R"({"code":200,"data":{"recommend":[]}})", R"({"code":200,"recommend":[null]})",
                 R"({"code":200,"recommend":[1]})"})
            row(json(b));
        const auto mutate = [&](const QString& k, QJsonValue v) {
            auto o = json(dailyPlaylistsFixture());
            auto a = o["recommend"].toArray();
            auto item = a[0].toObject();
            item[k] = v;
            a[0] = item;
            o["recommend"] = a;
            row(o);
        };
        for (const auto v : QList<QJsonValue>{
                 QJsonValue(), QJsonValue(true), QJsonValue(0), QJsonValue(1.5), QJsonValue("01"),
                 QJsonValue("9223372036854775808"), QJsonValue(qint64(9007199254740992LL))})
            mutate("id", v);
        for (const auto v :
             QList<QJsonValue>{QJsonValue(), QJsonValue(true), QJsonValue(QJsonObject{}),
                               QJsonValue(QString(4097, 'x')), QJsonValue(QString("x") + QChar(0)),
                               QJsonValue(QString(QChar(0xd800)))})
            mutate("name", v);
        for (const auto v : QList<QJsonValue>{QJsonValue(true), QJsonValue(QJsonArray{}),
                                              QJsonValue(QString(4097, 'x'))})
            mutate("copywriter", v);
        for (const auto v : QList<QJsonValue>{QJsonValue(-1), QJsonValue(true), QJsonValue("0"),
                                              QJsonValue(qint64(9007199254740992LL))})
            mutate("playcount", v);
        for (const auto k : QStringList{"trackCount", "updateTime"})
            for (const auto v : QList<QJsonValue>{QJsonValue(-1), QJsonValue(1.5), QJsonValue(true),
                                                  QJsonValue("0")})
                mutate(k, v);
        QJsonArray a;
        for (int i = 0; i < 101; ++i)
            a.append(QJsonObject{{"id", 1}, {"name", "synthetic"}});
        row({{"code", 200}, {"recommend", a}});
    }
    void badSchema()
    {
        QFETCH(QJsonObject, body);
        const auto r = mapMusicResponse(DailyRecommendedPlaylistsQuery{"101"}, body);
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void nativeAndGates()
    {
        Plan p{dailyPlaylistsFixture()};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotated; Domain=.music.163.com; Path=/"});
        Harness h({p, p});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        for (int i = 0; i < 2; ++i) {
            QVERIFY(h.service.request(token, DailyRecommendedPlaylistsQuery{"101"}));
            QTRY_COMPARE(done.size(), i + 1);
            QVERIFY(std::holds_alternative<DailyRecommendedPlaylists>(result(done)));
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            const auto req = h.http->requests.last();
            QCOMPARE(req.url(),
                     QUrl("https://music.163.com/weapi/v1/discovery/recommend/resource"));
            QVERIFY(req.rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
            QVERIFY(!req.rawHeader("Cookie").contains("rotated"));
            QVERIFY(!req.hasRawHeader("X-antiCheatToken"));
            QVERIFY(h.http->bodies.last().contains("encSecKey="));
        }
        QCOMPARE(h.http->requests.size(), 3);
        QVERIFY(h.service.request(token, DailyRecommendedPlaylistsQuery{"102"}));
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 3);
        for (int mode = 0; mode < 2; ++mode) {
            Harness denied({}, mode == 1, false);
            QSignalSpy d(&denied.service, &MusicQueryService::completed);
            QVERIFY(denied.service.request(denied.session.snapshot().token,
                                           DailyRecommendedPlaylistsQuery{"101"}));
            QTRY_COMPARE(d.size(), 1);
            QVERIFY(std::holds_alternative<MusicFailure>(result(d)));
            QCOMPARE(denied.http->requests.size(), 0);
        }
        Harness bounded({Plan{QByteArray(1024 * 1024 + 1, 'x')}});
        QSignalSpy b(&bounded.service, &MusicQueryService::completed);
        QVERIFY(bounded.service.request(bounded.session.snapshot().token,
                                        DailyRecommendedPlaylistsQuery{"101"}));
        QTRY_COMPARE(b.size(), 1);
        QVERIFY(std::holds_alternative<MusicFailure>(result(b)));
        QCOMPARE(bounded.http->requests.size(), 2);
    }
    void rejection_data()
    {
        QTest::addColumn<int>("mode");
        for (int n = 0; n < 9; ++n)
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void rejection()
    {
        QFETCH(int, mode);
        const QList<QByteArray> bodies{
            R"({"code":403})", R"({"code":429})",       R"({"code":-462})",
            R"({"code":500})", R"({"code":true})",      "{bad",
            R"({"code":200})", dailyPlaylistsFixture(), dailyPlaylistsFixture()};
        Harness h({Plan{bodies[mode], mode == 7 ? 429 : mode == 8 ? 502 : 200}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(
            h.service.request(h.session.snapshot().token, DailyRecommendedPlaylistsQuery{"101"}));
        QTRY_COMPARE(done.size(), 1);
        const auto e = std::get<MusicFailure>(result(done));
        QCOMPARE(e.error, mode == 1 || mode == 7 ? MusicError::RateLimited
                          : mode < 4             ? MusicError::ProviderRejected
                          : mode < 7             ? MusicError::InvalidResponse
                                                 : MusicError::Network);
        if (mode == 1 || mode == 7)
            QCOMPARE(e.retryAfterMs, std::optional<qint64>(3000));
        QCOMPARE(h.http->requests.size(), 2);
    }
    void cancel_data()
    {
        QTest::addColumn<int>("phase");
        for (int p = 0; p < 4; ++p)
            QTest::newRow(qPrintable(QString::number(p))) << p;
    }
    void cancel()
    {
        QFETCH(int, phase);
        Harness h({Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, DailyRecommendedPlaylistsQuery{"101"});
        QVERIFY(id);
        if (phase >= 2)
            QTRY_COMPARE(h.http->requests.size(), 2);
        if (phase % 2)
            h.session.signOut();
        else
            h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error,
                 phase % 2 ? MusicError::StaleContext : MusicError::Canceled);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    }
    void privateSameTokenScope_data()
    {
        QTest::addColumn<int>("mode");
        for (int m = 0; m < 3; ++m)
            QTest::newRow(qPrintable(QString::number(m))) << m;
    }
    void privateSameTokenScope()
    {
        QFETCH(int, mode);
        Harness h({Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto original = h.session.snapshot();
        QVERIFY(h.service.request(original.token, DailyRecommendedPlaylistsQuery{"101"}));
        QTRY_COMPARE(h.http->requests.size(), 2);
        auto changed = original;
        if (mode == 0)
            changed.accountId = "102";
        else
            changed.phase = mode == 1 ? SessionPhase::Guest : SessionPhase::Committing;
        emit h.session.changed(
            changed); // controlled same-token scope notification, not a real login
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), original.token);
    }
    void clientLoss_data()
    {
        QTest::addColumn<bool>("sent");
        QTest::newRow("queued") << false;
        QTest::newRow("transport") << true;
    }
    void clientLoss()
    {
        QFETCH(bool, sent);
        SessionCoordinator session;
        FakeManager* http = nullptr;
        auto client = std::make_unique<SessionApiClient>(Harness::transport({}, http), "synthetic");
        const auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
        QVERIFY(client->advanceTo(session.snapshot().token));
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans = {seed, Plan{"", 200, -1}};
        QSignalSpy seeded(client.get(), &SessionApiClient::completed);
        QVERIFY(client->submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(seeded.size(), 1);
        MusicGateway gateway(*client);
        MusicQueryService service(session, gateway);
        QSignalSpy done(&service, &MusicQueryService::completed);
        const auto token = session.snapshot().token;
        QVERIFY(service.request(token, DailyRecommendedPlaylistsQuery{"101"}));
        if (sent)
            QTRY_COMPARE(http->requests.size(), 2);
        client.reset();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Network);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    }
    void facadeAndCapacity()
    {
        BackendFacade f;
        QVERIFY(!f.dailyRecommendedPlaylists());
        ready(f);
        QSignalSpy calls(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            write(&f, &BackendFacade::collectionRequested),
            report(&f, &BackendFacade::listeningReportRequested);
        for (int i = 0; i < 16; ++i)
            QVERIFY(f.dailyRecommendedPlaylists());
        QCOMPARE(f.dailyRecommendedPlaylists(), quint64(0));
        for (const auto row : calls) {
            const auto c = qvariant_cast<MusicCommand>(row[0]);
            QCOMPARE(std::get<DailyRecommendedPlaylistsQuery>(c.query).accountId, "101");
            f.cancelMusic(c.id);
        }
        QTRY_COMPARE(done.size(), 16);
        QVERIFY(f.dailyRecommendedPlaylists());
        const auto c = qvariant_cast<MusicCommand>(calls.last()[0]);
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.session = f.snapshot();
        e.session.accountId = "102";
        f.deliver(e);
        QTRY_COMPARE(done.size(), 17);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        f.deliverMusic({c.id, c.generation, c.context,
                        mapMusicResponse(c.query, json(dailyPlaylistsFixture()))});
        QCOMPARE(done.size(), 17);
        QVERIFY(f.dailyRecommendedPlaylists());
        f.close();
        QTRY_COMPARE(done.size(), 18);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(play.isEmpty());
        QVERIFY(write.isEmpty());
        QVERIFY(report.isEmpty());
        Harness h(QList<Plan>(16, Plan{"", 200, -1}));
        QSignalSpy real(&h.service, &MusicQueryService::completed);
        for (int i = 0; i < 16; ++i)
            QVERIFY(h.service.request(h.session.snapshot().token,
                                      DailyRecommendedPlaylistsQuery{"101"}));
        QCOMPARE(
            h.service.request(h.session.snapshot().token, DailyRecommendedPlaylistsQuery{"101"}),
            quint64(0));
        QTRY_COMPARE(h.http->requests.size(), 17);
        h.session.signOut();
        QTRY_COMPARE(real.size(), 16);
    }
};
QTEST_GUILESS_MAIN(DailyPlaylistsTest)
#include "daily_playlists_test.moc"
