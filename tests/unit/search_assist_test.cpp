#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "../support/synthetic_search_assist.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include "infrastructure/netease/music/search_assist_mapper.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
MusicQuery query(int kind)
{
    if (kind == 0)
        return SearchSuggestionsQuery{"  synthetic \"\\文字😀  "};
    if (kind == 1)
        return HotKeywordsQuery{};
    return HotKeywordDetailsQuery{};
}
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last()[2]); }
MusicFailure failure(const QSignalSpy& spy) { return std::get<MusicFailure>(result(spy)); }
void ready(BackendFacade& f)
{
    f.start();
    BackendEvent e;
    e.kind = BackendEvent::Kind::Started;
    f.deliver(e);
    e.kind = BackendEvent::Kind::Session;
    e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
    f.deliver(e);
}
} // namespace
class SearchAssistTest : public QObject
{
    Q_OBJECT
  private slots:
    void valuesAndContracts()
    {
        QVERIFY(SearchSuggestionsQuery{QString(256, 'x')}.isValid());
        for (const auto& text :
             QStringList{"", " \t\n", QString(257, 'x'), QString(QChar(0)), QString(QChar(0xd800))})
            QVERIFY(!SearchSuggestionsQuery{text}.isValid());
        for (int k = 0; k < 3; ++k) {
            auto q = query(k);
            QVERIFY(validMusicQuery(q));
            QVERIFY(isSearchAssistQuery(q));
            QVERIFY(!requiresAuthenticatedMusicQuery(q));
            const auto request = searchAssistRequest(q);
            QVERIFY(request);
            auto contract = operationContract(request->operation, request->parameters);
            QVERIFY(contract);
            QCOMPARE(contract->protocol, k == 1 ? Protocol::Eapi : Protocol::Weapi);
            QCOMPARE(contract->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(contract->responseLimit, k == 2 ? 1024 * 1024 : 512 * 1024);
            const QString paths[]{"/api/search/suggest/web", "/api/search/hot",
                                  "/api/hotsearchlist/get"};
            QCOMPARE(contract->logicalPath, paths[k]);
            QVERIFY(!requiresAccountCookie(request->operation));
            const auto payload = operationPayload(request->operation, request->parameters);
            QVERIFY(payload);
            if (k == 0)
                QCOMPARE(*payload,
                         QJsonObject({{"s", std::get<SearchSuggestionsQuery>(q).keywords}}));
            else if (k == 1)
                QCOMPARE(*payload, QJsonObject({{"type", 1111}}));
            else
                QVERIFY(payload->isEmpty());
            for (QString field : {"s", "type", "limit", "offset", "crypto", "header", "checkToken",
                                  "e_r", "uid", "keywordsX"}) {
                auto p = request->parameters;
                p[field] = "synthetic";
                QVERIFY(!operationPayload(request->operation, p));
            }
            QVERIFY(!acceptsResponseCookies(
                request->operation,
                {DecodeError::None, searchAssistObject(R"({"code":200})"), 200}));
        }
        auto p = QJsonObject{{"keywords", 1}};
        QVERIFY(!operationPayload(ApiOperation::SearchSuggestions, p));
        QVERIFY(!searchAssistRequest(SongSearch{"synthetic"}));
    }
    void mapping()
    {
        const auto r = std::get<SearchSuggestions>(
            mapMusicResponse(query(0), searchAssistObject(searchAssistFixture(0))));
        QCOMPARE(r.query.keywords, std::get<SearchSuggestionsQuery>(query(0)).keywords);
        QVERIFY(r.unsupportedGroups);
        QCOMPARE(r.order, QList<SuggestionGroup>({SuggestionGroup::Artist, SuggestionGroup::Song,
                                                  SuggestionGroup::Album, SuggestionGroup::Playlist,
                                                  SuggestionGroup::Song}));
        QCOMPARE(r.songs->size(), 2);
        QCOMPARE(r.songs->first().id.value, "9007199254740993");
        QCOMPARE(r.songs->last().id.value, r.songs->first().id.value);
        QCOMPARE(r.artists->first().id.value, r.albums->first().id.value);
        QCOMPARE(r.songs->first().title, "<b>synthetic song</b>");
        const auto unknown = std::get<SearchSuggestions>(mapMusicResponse(
            query(0), searchAssistObject(R"({"code":200,"result":{"order":[]}})")));
        QVERIFY(!unknown.songs && !unknown.artists && !unknown.albums && !unknown.playlists);
        QVERIFY(!unknown.unsupportedGroups);
        const auto empty = std::get<SearchSuggestions>(mapMusicResponse(
            query(0),
            searchAssistObject(
                R"({"code":200,"result":{"order":["songs","artists"],"songs":[],"artists":[],"albums":null}})")));
        QVERIFY(empty.songs && empty.songs->isEmpty());
        QVERIFY(empty.artists && empty.artists->isEmpty());
        QVERIFY(!empty.albums);
        auto only = std::get<SearchSuggestions>(mapMusicResponse(
            query(0), searchAssistObject(
                          R"({"code":200,"result":{"order":["videos"],"videos":[{"id":99}]}})")));
        QVERIFY(only.unsupportedGroups);
        QVERIFY(only.order.isEmpty());
        QVERIFY(!only.songs);
        const auto hot = std::get<HotKeywords>(
            mapMusicResponse(query(1), searchAssistObject(searchAssistFixture(1))));
        QCOMPARE(hot.entries.size(), 2);
        QCOMPARE(hot.entries.first().keyword, "  synthetic 热搜  ");
        QCOMPARE(hot.entries.first().keyword, hot.entries.last().keyword);
        const auto detail = std::get<HotKeywordDetails>(
            mapMusicResponse(query(2), searchAssistObject(searchAssistFixture(2))));
        QCOMPARE(detail.entries.size(), 3);
        QCOMPARE(detail.entries.first().providerScore, std::optional<qint64>(0));
        QCOMPARE(detail.entries.first().description,
                 std::optional<QString>("<b>synthetic text</b>"));
        QCOMPARE(detail.entries.first().providerIconType, std::optional<qint64>(2));
        QCOMPARE(detail.entries.first().icon, QUrl("https://metadata.invalid/icon"));
        QVERIFY(!detail.entries[1].description && !detail.entries[1].providerScore &&
                !detail.entries[1].providerIconType);
        QCOMPARE(detail.entries[2].description, std::optional<QString>(""));
        QVERIFY(detail.entries[2].icon.isEmpty());
        QVERIFY(std::get<HotKeywords>(
                    mapMusicResponse(query(1),
                                     searchAssistObject(R"({"code":200,"result":{"hots":[]}})")))
                    .entries.isEmpty());
        QVERIFY(std::get<HotKeywordDetails>(
                    mapMusicResponse(query(2), searchAssistObject(R"({"code":200,"data":[]})")))
                    .entries.isEmpty());
    }
    void malformed_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QJsonObject>("body");
        int row = 0;
        const auto add = [&](int k, QJsonObject body) {
            QTest::newRow(qPrintable(QString::number(k) + "-" + QString::number(++row)))
                << k << body;
        };
        for (int k = 0; k < 3; ++k) {
            add(k, {});
            auto root = searchAssistObject(searchAssistFixture(k));
            root.remove("code");
            add(k, root);
            root["code"] = "200";
            add(k, root);
            for (const auto raw : QList<QJsonValue>{QJsonValue{}, true, "x", QJsonArray{}}) {
                root = searchAssistObject(searchAssistFixture(k));
                root[k == 2 ? "data" : "result"] =
                    (k == 2 && raw.isArray()) ? QJsonValue(QJsonObject{}) : raw;
                add(k, root);
            }
        }
        for (QString field : {"songs", "artists", "albums", "playlists"})
            for (const auto raw : QList<QJsonValue>{
                     QJsonValue{}, true, 1, "x", QJsonArray{true},
                     QJsonArray{QJsonObject{{"id", 0}, {"name", "synthetic"}}},
                     QJsonArray{QJsonObject{{"id", "01"}, {"name", "synthetic"}}},
                     QJsonArray{QJsonObject{{"id", 9007199254740992.0}, {"name", "synthetic"}}},
                     QJsonArray{QJsonObject{{"id", 1}, {"name", true}}},
                     QJsonArray{QJsonObject{{"id", 1}, {"name", QString(4097, 'x')}}}}) {
                auto root = searchAssistObject(searchAssistFixture(0));
                auto o = root["result"].toObject();
                o[field] = raw;
                root["result"] = o;
                add(0, root);
            }
        for (const auto raw : QList<QJsonValue>{QJsonValue{}, true, "x", QJsonArray{true},
                                                QJsonArray{""}, QJsonArray{QString(65, 'x')}}) {
            auto root = searchAssistObject(searchAssistFixture(0));
            auto o = root["result"].toObject();
            o["order"] = raw;
            root["result"] = o;
            add(0, root);
        }
        for (int k : {1, 2})
            for (const auto raw :
                 QList<QJsonValue>{QJsonArray{true}, QJsonArray{QJsonObject{}},
                                   QJsonArray{QJsonObject{{k == 1 ? "first" : "searchWord", ""}}},
                                   QJsonArray{QJsonObject{
                                       {k == 1 ? "first" : "searchWord", QString(257, 'x')}}}}) {
                auto root = searchAssistObject(searchAssistFixture(k));
                if (k == 1) {
                    auto o = root["result"].toObject();
                    o["hots"] = raw;
                    root["result"] = o;
                } else
                    root["data"] = raw;
                add(k, root);
            }
        for (QString field : {"score", "iconType"})
            for (const auto value : QList<QJsonValue>{true, -1, 1.25, 9007199254740992.0, "1"}) {
                auto root = searchAssistObject(searchAssistFixture(2));
                auto a = root["data"].toArray();
                auto o = a[0].toObject();
                o[field] = value;
                a[0] = o;
                root["data"] = a;
                add(2, root);
            }
        for (const auto value : QList<QJsonValue>{true, QString(4097, 'x'), QString(QChar(0))}) {
            auto root = searchAssistObject(searchAssistFixture(2));
            auto a = root["data"].toArray();
            auto o = a[0].toObject();
            o["content"] = value;
            a[0] = o;
            root["data"] = a;
            add(2, root);
        }
    }
    void malformed()
    {
        QFETCH(int, kind);
        QFETCH(QJsonObject, body);
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(query(kind), body)));
        QCOMPARE(std::get<MusicFailure>(mapMusicResponse(query(kind), body)).error,
                 MusicError::InvalidResponse);
    }
    void budgets()
    {
        for (int k = 0; k < 3; ++k) {
            auto root = searchAssistObject(searchAssistFixture(k));
            auto a = k == 0   ? root["result"].toObject()["songs"].toArray()
                     : k == 1 ? root["result"].toObject()["hots"].toArray()
                              : root["data"].toArray();
            const auto row = a.first();
            a = {};
            const int max = k == 0 ? 20 : 100;
            for (int n = 0; n <= max; ++n) {
                a.append(row);
                if (k == 2)
                    root["data"] = a;
                else {
                    auto o = root["result"].toObject();
                    o[k == 0 ? "songs" : "hots"] = a;
                    root["result"] = o;
                }
                if (n == max - 1)
                    QVERIFY(
                        !std::holds_alternative<MusicFailure>(mapMusicResponse(query(k), root)));
            }
            QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(query(k), root)));
        }
        auto root = searchAssistObject(searchAssistFixture(0));
        auto o = root["result"].toObject();
        QJsonArray order;
        for (int i = 0; i < 16; ++i)
            order.append("songs");
        o["order"] = order;
        root["result"] = o;
        QVERIFY(!std::holds_alternative<MusicFailure>(mapMusicResponse(query(0), root)));
        order.append("songs");
        o["order"] = order;
        root["result"] = o;
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(query(0), root)));
    }
    void native_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("auth");
        for (int k = 0; k < 3; ++k)
            for (bool auth : {false, true})
                QTest::newRow(qPrintable(QString::number(k) + "-" + QString::number(auth)))
                    << k << auth;
    }
    void native()
    {
        QFETCH(int, kind);
        QFETCH(bool, auth);
        SessionCoordinator session;
        auto manager = std::make_unique<FakeManager>();
        auto* http = manager.get();
        SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                                "synthetic-device");
        if (auth) {
            auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
            QVERIFY(p);
            QVERIFY(session.completeAuthentication(*p));
        }
        const auto token = session.snapshot().token;
        QVERIFY(client.advanceTo(token));
        if (auth) {
            Plan seed{R"({"code":200,"unikey":"synthetic"})"};
            seed.headers.append(std::pair<QByteArray, QByteArray>{
                "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
            http->plans = {seed};
            QSignalSpy done(&client, &SessionApiClient::completed);
            QVERIFY(client.submit(token, ApiOperation::QrKey));
            QTRY_COMPARE(done.size(), 1);
        }
        Plan response{searchAssistFixture(kind)};
        response.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-new; Domain=.music.163.com; Path=/"});
        http->plans = {response, response};
        MusicGateway gateway(client);
        MusicQueryService service(session, gateway);
        QSignalSpy done(&service, &MusicQueryService::completed);
        for (int i = 0; i < 2; ++i) {
            QVERIFY(service.request(token, query(kind)));
            QTRY_COMPARE(done.size(), i + 1);
            QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        }
        QCOMPARE(http->requests.size(), auth ? 3 : 2);
        const auto request = searchAssistRequest(query(kind));
        const auto payload = operationPayload(request->operation, request->parameters);
        const auto wire = http->requests.last();
        QCOMPARE(wire.url().host(),
                 kind == 1 ? QString("interfacepc.music.163.com") : QString("music.163.com"));
        QCOMPARE(wire.url().path(),
                 operationContract(request->operation)
                     ->logicalPath.replace("/api/", kind == 1 ? "/eapi/" : "/weapi/"));
        QVERIFY(wire.rawHeader("X-antiCheatToken").isEmpty());
        QVERIFY(!wire.rawHeader("Cookie").contains("synthetic-new"));
        QCOMPARE(wire.rawHeader("Cookie").contains("synthetic-original"), auth);
        if (kind == 1) {
            const auto decoded = decodeEapiRequest(http->bodies.last());
            QVERIFY(decoded);
            auto body = decoded->payload;
            QCOMPARE(body.take("e_r"), QJsonValue(false));
            QVERIFY(body.take("header").isObject());
            QCOMPARE(body, *payload);
        } else
            QVERIFY(http->bodies.last().startsWith("params="));
    }
    void isolation_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("stale");
        for (int k = 0; k < 3; ++k)
            for (bool stale : {false, true})
                QTest::newRow(qPrintable(QString::number(k) + "-" + QString::number(stale)))
                    << k << stale;
    }
    void isolation()
    {
        QFETCH(int, kind);
        QFETCH(bool, stale);
        SessionCoordinator session;
        auto manager = std::make_unique<FakeManager>();
        auto* http = manager.get();
        http->plans = {Plan{searchAssistFixture(kind), 200, -1}};
        SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)), "synthetic");
        const auto token = session.snapshot().token;
        QVERIFY(client.advanceTo(token));
        connect(&session, &SessionCoordinator::changed, &client,
                [&](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        MusicGateway gateway(client);
        MusicQueryService service(session, gateway);
        QSignalSpy done(&service, &MusicQueryService::completed);
        const auto id = service.request(token, query(kind));
        QVERIFY(id);
        QTRY_COMPARE(http->requests.size(), 1);
        if (stale)
            session.signOut();
        else
            service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(failure(done).error, stale ? MusicError::StaleContext : MusicError::Canceled);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QTest::qWait(5);
        QCOMPARE(done.size(), 1);
        QCOMPARE(http->requests.size(), 1);
    }
    void rejections_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("code");
        for (int k = 0; k < 3; ++k)
            for (int code : {403, 429, -462})
                QTest::newRow(qPrintable(QString::number(k) + "-" + QString::number(code)))
                    << k << code;
    }
    void rejections()
    {
        QFETCH(int, kind);
        QFETCH(int, code);
        auto manager = std::make_unique<FakeManager>();
        auto* http = manager.get();
        Plan p{QJsonDocument(QJsonObject{{"code", code}}).toJson(QJsonDocument::Compact)};
        // Plan already carries one Retry-After: 3; a duplicate becomes "3, 3".
        http->plans = {p};
        SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)), "synthetic");
        SessionCoordinator session;
        const auto token = session.snapshot().token;
        QVERIFY(client.advanceTo(token));
        MusicGateway gateway(client);
        QSignalSpy done(&gateway, &IMusicGateway::completed);
        QVERIFY(gateway.request(token, query(kind)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(failure(done).error,
                 code == 429 ? MusicError::RateLimited : MusicError::ProviderRejected);
        QCOMPARE(failure(done).providerCode, std::optional<int>(code));
        if (code == 429)
            QCOMPARE(failure(done).retryAfterMs, std::optional<qint64>(3000));
        QCOMPARE(http->requests.size(), 1);
    }
    void outOfOrder()
    {
        SessionCoordinator session;
        auto manager = std::make_unique<FakeManager>();
        auto* http = manager.get();
        http->plans = {Plan{searchAssistFixture(0), 200, 20}, Plan{searchAssistFixture(0)}};
        SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)), "synthetic");
        const auto token = session.snapshot().token;
        QVERIFY(client.advanceTo(token));
        MusicGateway gateway(client);
        MusicQueryService service(session, gateway);
        QSignalSpy done(&service, &MusicQueryService::completed);
        const auto first = service.request(token, SearchSuggestionsQuery{"synthetic first"});
        const auto second = service.request(token, SearchSuggestionsQuery{"synthetic second"});
        QVERIFY(first && second && first != second);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(done.first()[0].toULongLong(), second);
        QCOMPARE(done.last()[0].toULongLong(), first);
        QCOMPARE(
            std::get<SearchSuggestions>(qvariant_cast<MusicResult>(done.first()[2])).query.keywords,
            "synthetic second");
        QCOMPARE(std::get<SearchSuggestions>(result(done)).query.keywords, "synthetic first");
        QCOMPARE(http->requests.size(), 2);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    }
    void clientLossAndRawLimits()
    {
        for (int k = 0; k < 3; ++k) {
            SessionCoordinator session;
            auto manager = std::make_unique<FakeManager>();
            auto* http = manager.get();
            http->plans = {Plan{"", 200, -1}};
            auto client = std::make_unique<SessionApiClient>(
                std::make_unique<HttpTransport>(std::move(manager)), "synthetic");
            const auto token = session.snapshot().token;
            QVERIFY(client->advanceTo(token));
            MusicGateway gateway(*client);
            QSignalSpy done(&gateway, &IMusicGateway::completed);
            QVERIFY(gateway.request(token, query(k)));
            QTRY_COMPARE(http->requests.size(), 1);
            client.reset();
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(failure(done).error, MusicError::Network);
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            QTest::qWait(1);
            QCOMPARE(done.size(), 1);
            auto large = std::make_unique<FakeManager>();
            auto* observed = large.get();
            const auto req = searchAssistRequest(query(k));
            const auto limit = operationContract(req->operation)->responseLimit;
            large->plans = {Plan{QByteArray(limit + 1, 'x')}};
            SessionApiClient bounded(std::make_unique<HttpTransport>(std::move(large)),
                                     "synthetic");
            QVERIFY(bounded.advanceTo(token));
            MusicGateway receiver(bounded);
            QSignalSpy rejected(&receiver, &IMusicGateway::completed);
            QVERIFY(receiver.request(token, query(k)));
            QTRY_COMPARE(rejected.size(), 1);
            QVERIFY(std::holds_alternative<MusicFailure>(result(rejected)));
            QCOMPARE(observed->requests.size(), 1);
        }
    }
    void facade()
    {
        BackendFacade f;
        QVERIFY(!f.hotKeywords());
        ready(f);
        QSignalSpy commands(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            report(&f, &BackendFacade::listeningReportRequested),
            write(&f, &BackendFacade::commentWriteRequested);
        QVERIFY(!f.searchSuggestions(""));
        QVERIFY(f.searchSuggestions("synthetic"));
        QVERIFY(f.hotKeywords());
        QVERIFY(f.hotKeywordDetails());
        QCOMPARE(commands.size(), 3);
        const auto c = qvariant_cast<MusicCommand>(commands.first()[0]);
        QCOMPARE(std::get<SearchSuggestionsQuery>(c.query).keywords, "synthetic");
        for (int i = 3; i < 16; ++i)
            QVERIFY(f.hotKeywords());
        QVERIFY(!f.hotKeywordDetails());
        f.cancelMusic(c.id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(failure(done).error, MusicError::Canceled);
        f.deliverMusic({c.id, c.generation, c.context, HotKeywords{}});
        QCOMPARE(done.size(), 1);
        QVERIFY(play.isEmpty());
        QVERIFY(report.isEmpty());
        QVERIFY(write.isEmpty());
        f.close();
        QTRY_COMPARE(done.size(), 16);
        QCOMPARE(failure(done).error, MusicError::Canceled);
    }
};
QTEST_GUILESS_MAIN(SearchAssistTest)
#include "search_assist_test.moc"
