#include "../support/fake_http.h"
#include "../support/synthetic_discovery_browse.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/api/discovery_browse_operation.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
MusicResult last(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last()[2]); }
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
    explicit Harness(QList<Plan> plans)
        : client(transport(plans, http), "synthetic-discovery-browse")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        client.setAnonymousToken("synthetic-anon");
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
    }
};
} // namespace
class DiscoveryBrowseTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts_data()
    {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 9; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void contracts()
    {
        QFETCH(int, kind);
        const auto q = discoveryBrowseQueries()[kind];
        const auto r = discoveryBrowseRequest(q);
        QVERIFY(r);
        QVERIFY(isDiscoveryBrowseOperation(r->operation));
        const auto c = operationContract(r->operation);
        QVERIFY(c);
        QCOMPARE(c->protocol, Protocol::Weapi);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QCOMPARE(c->destination, OfficialDestination::Default);
        QCOMPARE(c->responseLimit, 2 * 1024 * 1024);
        const QStringList paths{"/api/discovery/newAlbum",
                                "/api/album/new",
                                "/api/discovery/new/albums/area",
                                "/api/v1/artist/list",
                                "/api/artist/top",
                                "/api/artist/top/song",
                                "/api/v1/discovery/simiSong",
                                "/api/discovery/simiArtist",
                                "/api/discovery/simiPlaylist"};
        QCOMPARE(c->logicalPath, paths[kind]);
        QVERIFY(!requiresAccountCookie(r->operation));
        QVERIFY(!requiresAuthenticatedMusicQuery(q));
        QVERIFY(!musicQueryAccount(q));
        auto expected = r->parameters;
        if (kind == 1 || kind == 3 || kind == 4)
            expected.insert("total", true);
        if (kind == 2) {
            expected.insert("total", false);
            expected.insert("rcmd", true);
        }
        QCOMPARE(*operationPayload(r->operation, r->parameters), expected);
        if (kind == 0)
            QVERIFY(expected.isEmpty());
        if (kind == 3) {
            QCOMPARE(expected.value("type"), QJsonValue("3"));
            QCOMPARE(expected.value("area"), QJsonValue("0"));
            QCOMPARE(expected.value("initial"), QJsonValue(65));
        }
        if (kind == 6 || kind == 8)
            QCOMPARE(expected.value("songid"), QJsonValue("9007199254740995"));
        if (kind == 2) {
            QCOMPARE(expected.value("year"), QJsonValue(2026));
            QCOMPARE(expected.value("month"), QJsonValue(10));
        }
        auto bad = r->parameters;
        bad.insert("host", "https://example.invalid");
        QVERIFY(!operationPayload(r->operation, bad));
        for (const auto& key : r->parameters.keys()) {
            bad = r->parameters;
            bad.remove(key);
            QVERIFY(!operationPayload(r->operation, bad));
            bad = r->parameters;
            bad.insert(key, QJsonValue(QJsonValue::Null));
            QVERIFY(!operationPayload(r->operation, bad));
            bad = r->parameters;
            bad.insert(key, true);
            QVERIFY(!operationPayload(r->operation, bad));
            if (r->parameters.value(key).isDouble()) {
                bad = r->parameters;
                bad.insert(key, 1.5);
                QVERIFY(!operationPayload(r->operation, bad));
            }
        }
        DecodedResponse response;
        response.object = discoveryBrowseResponse(kind);
        response.businessCode = 200;
        QVERIFY(!acceptsResponseCookies(r->operation, response));
    }
    void invalidQueries()
    {
        const QList<DiscoveryBrowseQuery> bad{
            {NewAlbumsQuery{static_cast<AlbumArea>(5)}},
            {NewAlbumsQuery{AlbumArea::All, {0, 0}}},
            {AlbumChartsQuery{}},
            {AlbumChartsQuery{1899, 1}},
            {AlbumChartsQuery{2026, 13}},
            {ArtistBrowseQuery{static_cast<ArtistCategory>(0)}},
            {ArtistBrowseQuery{ArtistCategory::All, static_cast<ArtistArea>(2)}},
            {ArtistBrowseQuery{ArtistCategory::All, ArtistArea::All, 97}},
            {PopularArtistsQuery{{101, 0}}},
            {ArtistTopSongsQuery{{"0"}}},
            {SimilarSongsQuery{{"01"}}},
            {SimilarArtistsQuery{{"-1"}}},
            {SimilarPlaylistsQuery{{"1"}, {1, -1}}}};
        for (const auto& q : bad) {
            QVERIFY(!q.isValid());
            QVERIFY(!discoveryBrowseRequest(q));
        }
        QVERIFY(ArtistBrowseQuery{}.isValid());
        QVERIFY((ArtistBrowseQuery{ArtistCategory::All, ArtistArea::All, 0}).isValid());
        QVERIFY((AlbumChartsQuery{9999, 12}).isValid());
        QVERIFY(!isDiscoveryBrowseOperation(ApiOperation::TogetherDeviceNotice));
        QVERIFY(!isDiscoveryBrowseOperation(
            static_cast<ApiOperation>(int(ApiOperation::SimilarPlaylists) + 1)));
    }
    void metadata_data() { contracts_data(); }
    void metadata()
    {
        QFETCH(int, kind);
        const auto q = discoveryBrowseQueries()[kind];
        const auto r = mapMusicResponse(q, discoveryBrowseResponse(kind));
        QVERIFY(std::holds_alternative<DiscoveryBrowsePage>(r));
        const auto p = std::get<DiscoveryBrowsePage>(r);
        QVERIFY(matchingDiscoveryBrowsePage(q, p));
        std::visit(
            [](const auto& items) {
                if constexpr (requires { items.week; }) {
                    QCOMPARE(items.week.size(), 1);
                    QCOMPARE(items.month.size(), 2);
                } else if constexpr (requires { items.albums; })
                    QCOMPARE(items.albums.size(), 2);
                else if constexpr (requires { items.artists; })
                    QCOMPARE(items.artists.size(), 2);
                else if constexpr (requires { items.songs; })
                    QCOMPARE(items.songs.size(), 2);
                else
                    QCOMPARE(items.playlists.size(), 2);
            },
            p.content);
        QVERIFY(!p.total);
        QVERIFY(!p.hasMore);
        auto empty = discoveryBrowseResponse(kind);
        for (auto i = empty.begin(); i != empty.end(); ++i)
            if (i.value().isArray())
                i.value() = QJsonArray{};
        QVERIFY(std::holds_alternative<DiscoveryBrowsePage>(mapMusicResponse(q, empty)));
        if (kind == 1) {
            empty.insert("total", 0);
            empty.insert("hasMore", false);
            const auto page = std::get<DiscoveryBrowsePage>(mapMusicResponse(q, empty));
            QCOMPARE(page.total, std::optional<qint64>(0));
            QCOMPARE(page.hasMore, std::optional<bool>(false));
        }
    }
    void malformed_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("fault");
        for (int k = 0; k < 9; ++k)
            for (int f = 0; f < 10; ++f)
                QTest::newRow(qPrintable(QString("%1-%2").arg(k).arg(f))) << k << f;
    }
    void malformed()
    {
        QFETCH(int, kind);
        QFETCH(int, fault);
        auto o = discoveryBrowseResponse(kind);
        QString field;
        for (auto i = o.begin(); i != o.end(); ++i)
            if (i.value().isArray()) {
                field = i.key();
                break;
            }
        if (fault == 0)
            o.remove(field);
        if (fault == 1)
            o.insert(field, QJsonValue(QJsonValue::Null));
        if (fault == 2)
            o.insert(field, QJsonObject{});
        if (fault == 3)
            o.insert(field, QJsonArray{QJsonValue(1)});
        if (fault >= 4 && fault <= 7) {
            auto item = o.value(field).toArray().first().toObject();
            if (fault == 4)
                item.remove("name");
            if (fault == 5)
                item.insert("name", QString(4097, u'x'));
            if (fault == 6)
                item.insert("id", 9007199254740992LL);
            if (fault == 7)
                item.insert("id", "01");
            o.insert(field, QJsonArray{item});
        }
        if (fault == 8)
            o.insert("code", "200");
        if (fault == 9)
            o.insert("unknown", QString(2 * 1024 * 1024, u'x'));
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(discoveryBrowseQueries()[kind], o)));
    }
    void boundsAndEchoes()
    {
        for (int k = 0; k < 9; ++k) {
            auto o = discoveryBrowseResponse(k);
            QString field;
            for (auto i = o.begin(); i != o.end(); ++i)
                if (i.value().isArray()) {
                    field = i.key();
                    break;
                }
            const auto item = o.value(field).toArray().first();
            QJsonArray a;
            for (int i = 0; i < 501; ++i)
                a.append(item);
            o.insert(field, a);
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapMusicResponse(discoveryBrowseQueries()[k], o)));
        }
        auto o = discoveryBrowseResponse(1);
        o.insert("total", 1);
        QVERIFY(
            std::holds_alternative<MusicFailure>(mapMusicResponse(discoveryBrowseQueries()[1], o)));
        o = discoveryBrowseResponse(4);
        o.insert("more", 1);
        QVERIFY(
            std::holds_alternative<MusicFailure>(mapMusicResponse(discoveryBrowseQueries()[4], o)));
        for (int k : {5, 6, 7, 8}) {
            o = discoveryBrowseResponse(k);
            o.insert(k == 5 || k == 7 ? "artistId" : "songId", "999");
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapMusicResponse(discoveryBrowseQueries()[k], o)));
        }
    }
    void nativeReads_data() { contracts_data(); }
    void nativeReads()
    {
        QFETCH(int, kind);
        Plan plan{discoveryBrowseBody(kind)};
        plan.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-unadopted; Domain=.music.163.com; Path=/"});
        Harness h({plan});
        const auto token = h.session.snapshot().token;
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(token, discoveryBrowseQueries()[kind]));
        QTRY_COMPARE(spy.size(), 1);
        QVERIFY(std::holds_alternative<DiscoveryBrowsePage>(last(spy)));
        QCOMPARE(h.http->requests.size(), 1);
        QCOMPARE(h.http->requests[0].url().host(), QString("music.163.com"));
        const auto c = discoveryBrowseContract(
            discoveryBrowseRequest(discoveryBrowseQueries()[kind])->operation);
        QCOMPARE(h.http->requests[0].url().path(),
                 QString(c->logicalPath).replace("/api/", "/weapi/"));
        QVERIFY(h.http->bodies[0].startsWith("params="));
        QVERIFY(h.http->bodies[0].contains("&encSecKey="));
        QVERIFY(!h.http->bodies[0].contains("synthetic-unadopted"));
        const auto report = operationPayload(ApiOperation::ListeningStart, {});
        QVERIFY(!report); // no implicit report or credential adoption
        QSignalSpy api(&h.client, &SessionApiClient::completed);
        QVERIFY(h.client.submit(token, ApiOperation::MyPlaylists,
                                {{"uid", "101"}, {"limit", 1}, {"offset", 0}}));
        QTRY_COMPARE(api.size(), 1);
        QCOMPARE(qvariant_cast<ApiResult>(api.last()[2]).error, ApiError::Cookie);
        QCOMPARE(h.http->requests.size(), 1); // response Cookie did not authenticate Guest
    }
    void cancellationSlotsAndEpoch()
    {
        Harness h(QList<Plan>(16, Plan{"{}", 200, -1}));
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        QList<quint64> ids;
        for (int i = 0; i < 16; ++i) {
            const auto id = h.service.request(token, discoveryBrowseQueries()[i % 9]);
            QVERIFY(id);
            ids.append(id);
        }
        QCOMPARE(h.service.request(token, discoveryBrowseQueries()[0]), 0);
        QTRY_COMPARE(h.http->requests.size(), 16);
        h.service.cancel(ids.first());
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(std::get<MusicFailure>(last(spy)).error, MusicError::Canceled);
        (void)h.session.beginAuthentication();
        QTRY_COMPARE(spy.size(), 16);
        for (int i = 1; i < spy.size(); ++i)
            QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(spy[i][2])).error,
                     MusicError::StaleContext);
    }
    void facadeCorrelationAndClose()
    {
        BackendFacade f;
        SessionCoordinator session;
        f.start();
        BackendEvent start;
        start.kind = BackendEvent::Kind::Started;
        f.deliver(start);
        BackendEvent event;
        event.kind = BackendEvent::Kind::Session;
        event.session = session.snapshot();
        f.deliver(event);
        QSignalSpy requests(&f, &BackendFacade::musicRequested),
            results(&f, &BackendFacade::musicCompleted);
        const auto q = discoveryBrowseQueries()[6];
        const auto id = f.discover(q);
        QVERIFY(id);
        const auto c = qvariant_cast<MusicCommand>(requests.last()[0]);
        auto page = std::get<DiscoveryBrowsePage>(mapMusicResponse(q, discoveryBrowseResponse(6)));
        std::get<SimilarSongsQuery>(page.query.action).song.value = "999";
        f.deliverMusic({id, c.generation, c.context, page});
        QCOMPARE(results.size(), 1);
        QCOMPARE(std::get<MusicFailure>(last(results)).error, MusicError::InvalidResponse);
        QVERIFY(f.discover(q));
        f.close();
        QTRY_COMPARE(results.size(), 2);
        QVERIFY(std::holds_alternative<MusicFailure>(last(results)));
        QCOMPARE(f.discover(q), 0);
        f.finishClose(false);
    }
    void scopedCancel_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("mode");
        for (int k = 0; k < 9; ++k)
            for (int m = 0; m < 4; ++m)
                QTest::newRow(qPrintable(QString("%1-%2").arg(k).arg(m))) << k << m;
    }
    void scopedCancel()
    {
        QFETCH(int, kind);
        QFETCH(int, mode);
        Harness h({Plan{discoveryBrowseBody(kind), 200, -1}});
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, discoveryBrowseQueries()[kind]);
        QVERIFY(id);
        if (mode >= 2)
            QTRY_COMPARE(h.http->requests.size(), 1);
        if (mode % 2 == 0)
            h.service.cancel(id);
        else
            (void)h.session.beginAuthentication();
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(std::get<MusicFailure>(last(spy)).error,
                 mode % 2 == 0 ? MusicError::Canceled : MusicError::StaleContext);
        QTest::qWait(40);
        QCOMPARE(spy.size(), 1);
        if (mode < 2)
            QCOMPARE(h.http->requests.size(), 0);
    }
    void providers_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<MusicError>("error");
        QTest::newRow("403") << QByteArray(R"({"code":403})") << 200
                             << MusicError::ProviderRejected;
        QTest::newRow("429") << QByteArray(R"({"code":429})") << 200 << MusicError::RateLimited;
        QTest::newRow("bad-schema")
            << QByteArray(R"({"code":200,"albums":null})") << 200 << MusicError::InvalidResponse;
        QTest::newRow("bad-json") << QByteArray("{") << 200 << MusicError::InvalidResponse;
        QTest::newRow("http") << QByteArray(R"({"code":503})") << 503 << MusicError::Network;
    }
    void providers()
    {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(MusicError, error);
        Harness h({Plan{body, status}});
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, discoveryBrowseQueries()[0]));
        QTRY_COMPARE(spy.size(), 1);
        const auto f = std::get<MusicFailure>(last(spy));
        QCOMPARE(f.error, error);
        QCOMPARE(h.http->requests.size(), 1);
        if (error == MusicError::RateLimited)
            QCOMPARE(f.retryAfterMs, std::optional<qint64>(3000));
    }
    void typedMetadataAndCorrelationGuards()
    {
        for (int k = 0; k < 9; ++k) {
            const auto q = discoveryBrowseQueries()[k];
            const auto p =
                std::get<DiscoveryBrowsePage>(mapMusicResponse(q, discoveryBrowseResponse(k)));
            std::visit(
                [](const auto& items) {
                    if constexpr (requires { items.week; })
                        QCOMPARE(items.week.first().id.value, QString("9007199254740997"));
                    else if constexpr (requires { items.albums; }) {
                        QCOMPARE(items.albums.first().id.value, QString("9007199254740997"));
                        QCOMPARE(items.albums.first().title, QString(" synthetic 雲 & <text> "));
                    } else if constexpr (requires { items.artists; }) {
                        QCOMPARE(items.artists.first().id.value, QString("9007199254740997"));
                        QCOMPARE(items.artists.first().name, QString(" synthetic 雲 & <text> "));
                    } else if constexpr (requires { items.songs; }) {
                        QCOMPARE(items.songs.first().id.value, QString("9007199254740997"));
                        QCOMPARE(items.songs.first().title, QString(" synthetic 雲 & <text> "));
                    } else {
                        QCOMPARE(items.playlists.first().id.value, QString("9007199254740997"));
                        QCOMPARE(items.playlists.first().title, QString(" synthetic 雲 & <text> "));
                    }
                },
                p.content);
            auto other = q;
            std::visit(
                [](auto& value) {
                    if constexpr (requires { value.page; })
                        ++value.page.offset;
                    else if constexpr (requires { value.artist; })
                        value.artist.value = "999";
                    else
                        value = LatestAlbumsQuery{};
                },
                other.action);
            if (k != 0)
                QVERIFY(!sameDiscoveryBrowseQuery(q, other));
            auto wrong = p;
            wrong.content = DiscoveryAlbumCharts{};
            if (k != 2)
                QVERIFY(!matchingDiscoveryBrowsePage(q, wrong));
        }
        for (const QJsonValue bad :
             {QJsonValue(true), QJsonValue(1.5), QJsonValue(-1), QJsonValue("3")}) {
            auto o = discoveryBrowseResponse(3);
            auto a = o["artists"].toArray();
            auto row = a.first().toObject();
            row.insert("musicSize", bad);
            a[0] = row;
            o["artists"] = a;
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapMusicResponse(discoveryBrowseQueries()[3], o)));
        }
    }
};
QTEST_GUILESS_MAIN(DiscoveryBrowseTest)
#include "discovery_browse_test.moc"
