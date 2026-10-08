#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "../support/synthetic_song_wiki_text.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
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
MusicResult last(const QSignalSpy& s) { return qvariant_cast<MusicResult>(s.last()[2]); }
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
    explicit Harness(QList<Plan> plans) : client(transport(plans, http), "synthetic-wiki-text")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        client.setAnonymousToken("synthetic-anon");
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
    }
};
QJsonObject malformed(int k)
{
    auto o = wikiTextObject();
    auto data = o["data"].toObject();
    auto outer = data["blocks"].toArray();
    auto main = outer[1].toObject();
    auto p = main["rnData"].toObject();
    auto groups = p["blocks"].toArray();
    auto g = groups[0].toObject();
    auto info = g["blockInfo"].toObject();
    auto rows = info["wikiSubElementVos"].toArray();
    auto row = rows[0].toObject();
    auto metas = row["wikiSubMetaVos"].toArray();
    auto meta = metas[0].toObject();
    switch (k) {
    case 0:
        meta.remove("text");
        break;
    case 1:
        meta["text"] = 42;
        break;
    case 2:
        meta["text"] = QString(4097, 'x');
        break;
    }
    metas[0] = meta;
    switch (k) {
    case 3:
        metas[0] = 42;
        break;
    case 4:
        while (metas.size() < 17)
            metas.append(meta);
        break;
    }
    row["wikiSubMetaVos"] = metas;
    switch (k) {
    case 5:
        row.remove("title");
        break;
    case 6:
        row["title"] = 42;
        break;
    case 7:
        row["title"] = QString(257, 'x');
        break;
    case 8:
        row["content"] = QJsonValue::Null;
        break;
    case 9:
        row["content"] = 42;
        break;
    case 10:
        row["content"] = QString(4097, 'x');
        break;
    case 11:
        row["wikiSubMetaVos"] = QJsonValue::Null;
        break;
    case 12:
        row["wikiSubMetaVos"] = QJsonObject{};
        break;
    case 13:
        row.remove("wikiSubMetaVos");
        break;
    }
    rows[0] = row;
    switch (k) {
    case 14:
        rows[0] = 42;
        break;
    case 15:
        while (rows.size() < 101)
            rows.append(row);
        break;
    }
    info["wikiSubElementVos"] = rows;
    switch (k) {
    case 16:
        info["blockName"] = QJsonValue::Null;
        break;
    case 17:
        info["blockName"] = QString(257, 'x');
        break;
    case 18:
        info["desc"] = QJsonValue::Null;
        break;
    case 19:
        info["desc"] = QString(4097, 'x');
        break;
    case 20:
        info.remove("wikiSubElementVos");
        break;
    case 21:
        info["wikiSubElementVos"] = QJsonValue::Null;
        break;
    case 22:
        info["wikiSubElementVos"] = QJsonObject{};
        break;
    }
    g["blockInfo"] = info;
    switch (k) {
    case 23:
        g["blockInfo"] = QJsonValue::Null;
        break;
    case 24:
        g["blockCode"] = 42;
        break;
    }
    groups[0] = g;
    if (k == 25)
        while (groups.size() < 33)
            groups.append(QJsonObject{{"blockCode", "syntheticUnsupported"}});
    p["blocks"] = groups;
    switch (k) {
    case 26:
        p.remove("blocks");
        break;
    case 27:
        p["blocks"] = QJsonValue::Null;
        break;
    case 28:
        p["blocks"] = QJsonObject{};
        break;
    case 29:
        p["songName"] = "different synthetic title";
        break;
    case 30:
        p["songName"] = QJsonValue::Null;
        break;
    case 31:
        p["songName"] = QString(4097, 'x');
        break;
    case 32:
        p["albumId"] = "51";
        break;
    case 33:
        p["albumId"] = "09007199254740995";
        break;
    case 34:
        p["albumId"] = 9007199254740992.;
        break;
    }
    main["rnData"] = p;
    switch (k) {
    case 35:
        main["bizCode"] = "different";
        break;
    case 36:
        main["rnData"] = QJsonValue::Null;
        break;
    case 37:
        main["positionCode"] = 42;
        break;
    }
    outer[1] = main;
    switch (k) {
    case 38:
        outer.append(main);
        break;
    case 39:
        outer = QJsonArray{};
        break;
    case 40:
        outer[0] = 42;
        break;
    case 41:
        outer[0] = QJsonObject{};
        break;
    case 42:
        while (outer.size() < 33)
            outer.append(QJsonObject{{"positionCode", "syntheticUnsupported"}});
        break;
    }
    data["blocks"] = outer;
    switch (k) {
    case 43:
        data["blocks"] = QJsonValue::Null;
        break;
    case 44:
        data["blocks"] = QJsonObject{};
        break;
    case 45:
        data.remove("blocks");
        break;
    }
    o["data"] = data;
    switch (k) {
    case 46:
        o["data"] = QJsonValue::Null;
        break;
    case 47:
        o["data"] = QJsonArray{};
        break;
    case 48:
        o.remove("data");
        o["blocks"] = outer;
        break;
    case 49:
        o["code"] = "200";
        break;
    case 50:
        o["ignoredHuge"] = QString(2 * 1024 * 1024, 'x');
        break;
    }
    return o;
}
} // namespace
class SongWikiTextTest : public QObject
{
    Q_OBJECT
  private slots:
    void originFactory()
    {
        SessionCoordinator session;
        auto s = wikiTextSong();
        const auto t = session.snapshot().token;
        auto q = SongWikiTextQuery::fromSong(t, s);
        QVERIFY(q);
        QVERIFY(q->isValid());
        QCOMPARE(q->song().value, s.id.value);
        QCOMPARE(q->sourceTitle(), s.title);
        QCOMPARE(q->sourceAlbum().value, s.album->id->value);
        QCOMPARE(musicQuerySourceContext(MusicQuery{*q}).value(), t);
        QVERIFY(validMusicQuery(*q));
        QVERIFY(!requiresAuthenticatedMusicQuery(*q));
        QVERIFY(!SongWikiTextQuery::fromSong({}, s));
        s.id.value = "01";
        QVERIFY(!SongWikiTextQuery::fromSong(t, s));
        s = wikiTextSong();
        s.title.clear();
        QVERIFY(!SongWikiTextQuery::fromSong(t, s));
        s.title = QString(4097, 'x');
        QVERIFY(!SongWikiTextQuery::fromSong(t, s));
        s = wikiTextSong();
        s.album.reset();
        QVERIFY(!SongWikiTextQuery::fromSong(t, s));
        s = wikiTextSong();
        s.album->id.reset();
        QVERIFY(!SongWikiTextQuery::fromSong(t, s));
    }
    void contract()
    {
        const auto c = operationContract(ApiOperation::SongWikiText);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, "/api/link/page/parent/relation/construct/info");
        QCOMPARE(c->protocol, Protocol::Eapi);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QCOMPARE(c->destination, OfficialDestination::Default);
        QCOMPARE(c->responseLimit, 2 * 1024 * 1024);
        QVERIFY(!requiresAccountCookie(ApiOperation::SongWikiText));
        const auto p =
            operationPayload(ApiOperation::SongWikiText, {{"id", wikiTextSong().id.value}});
        QVERIFY(p);
        QCOMPARE(p->size(), 2);
        QCOMPARE(p->value("positionCode").toString(), "songWikiMainPosition");
        const auto resource = QJsonDocument::fromJson(p->value("extJson").toString().toUtf8())
                                  .object()["states"]
                                  .toObject()["playingResource"]
                                  .toObject();
        QCOMPARE(resource["current"].toString(), wikiTextSong().id.value);
        QCOMPARE(resource["scene"].toString(), "songWiki");
        DecodedResponse r;
        r.businessCode = 200;
        r.object = {{"code", 200}};
        QVERIFY(!acceptsResponseCookies(ApiOperation::SongWikiText, r));
    }
    void invalidPayload_data()
    {
        QTest::addColumn<QJsonObject>("p");
        QTest::newRow("missing") << QJsonObject{};
        QTest::newRow("extra") << QJsonObject{{"id", "41"}, {"mode", 1}};
        QTest::newRow("number") << QJsonObject{{"id", 41}};
        QTest::newRow("null") << QJsonObject{{"id", QJsonValue::Null}};
        for (const auto& s : QStringList{"", "0", "01", "-1", " 41 ", "1e2"})
            QTest::newRow(qPrintable("bad-" + s)) << QJsonObject{{"id", s}};
    }
    void invalidPayload()
    {
        QFETCH(QJsonObject, p);
        QVERIFY(!operationPayload(ApiOperation::SongWikiText, p));
    }
    void subset()
    {
        SessionCoordinator session;
        const auto q = wikiTextQuery(session.snapshot().token);
        const auto r = mapMusicResponse(q, wikiTextObject());
        QVERIFY(std::holds_alternative<SongWikiText>(r));
        const auto page = std::get<SongWikiText>(r);
        QVERIFY(matchingSongWikiText(q, page));
        QCOMPARE(page.identity, SongWikiIdentity::NotIndependentlyVerified);
        QCOMPARE(page.unsupportedBlockCount, 3);
        QVERIFY(page.metadataOmitted);
        QCOMPARE(page.sections.size(), 2);
        QCOMPARE(page.sections[0].kind, SongWikiTextSectionKind::SongInfo);
        QCOMPARE(page.sections[1].kind, SongWikiTextSectionKind::BaseInfo);
        QCOMPARE(page.sections[0].items.size(), 2);
        QCOMPARE(*page.sections[0].items[0].texts,
                 QStringList({" synthetic & <text> ", " synthetic & <text> "}));
        QVERIFY(!page.sections[0].items[0].content);
        QVERIFY(!page.sections[1].description);
        QCOMPARE(*page.sections[1].items[0].content, " synthetic content ");
    }
    void malformed_data()
    {
        QTest::addColumn<int>("kind");
        for (int k = 0; k <= 50; ++k)
            QTest::newRow(qPrintable(QString::number(k))) << k;
    }
    void malformed()
    {
        QFETCH(int, kind);
        SessionCoordinator session;
        const auto r = mapMusicResponse(wikiTextQuery(session.snapshot().token), ::malformed(kind));
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void emptyAndUnsupported()
    {
        SessionCoordinator session;
        const auto q = wikiTextQuery(session.snapshot().token);
        auto o = wikiTextObject();
        auto data = o["data"].toObject();
        auto outer = data["blocks"].toArray();
        auto main = outer[1].toObject();
        auto p = main["rnData"].toObject();
        p["songId"] = "not-an-independent-binding";
        p["blocks"] = QJsonArray{QJsonObject{{"blockCode", "syntheticUnknown"}, {"blockInfo", 42}}};
        main["rnData"] = p;
        outer[1] = main;
        data["blocks"] = outer;
        o["data"] = data;
        auto r = mapMusicResponse(q, o);
        QVERIFY(std::holds_alternative<SongWikiText>(r));
        QVERIFY(std::get<SongWikiText>(r).sections.isEmpty());
        QCOMPARE(std::get<SongWikiText>(r).identity, SongWikiIdentity::NotIndependentlyVerified);
        p["blocks"] = QJsonArray{QJsonObject{
            {"blockCode", "wikiSubBlockBaseInfoVo"},
            {"blockInfo", QJsonObject{{"blockName", ""}, {"wikiSubElementVos", QJsonArray{}}}}}};
        main["rnData"] = p;
        outer[1] = main;
        data["blocks"] = outer;
        o["data"] = data;
        r = mapMusicResponse(q, o);
        QVERIFY(std::holds_alternative<SongWikiText>(r));
        QCOMPARE(std::get<SongWikiText>(r).sections.size(), 1);
        QVERIFY(std::get<SongWikiText>(r).sections[0].items.isEmpty());
    }
    void bounds_data()
    {
        QTest::addColumn<int>("kind");
        for (int k = 0; k < 8; ++k)
            QTest::newRow(qPrintable(QString::number(k))) << k;
    }
    void bounds()
    {
        QFETCH(int, kind);
        SessionCoordinator session;
        const auto q = wikiTextQuery(session.snapshot().token);
        auto o = wikiTextObject();
        auto data = o["data"].toObject();
        auto outer = data["blocks"].toArray();
        auto main = outer[1].toObject();
        auto p = main["rnData"].toObject();
        QJsonArray rows;
        QJsonObject info{{"blockName", ""}};
        if (kind == 0)
            info["blockName"] = QString(256, 'x');
        if (kind == 1)
            info["desc"] = QString(4096, 'x');
        if (kind < 6) {
            QJsonObject row{{"title", kind == 2 ? QString(256, 'x') : QString{}},
                            {"content", kind == 3 ? QString(4096, 'x') : QString{}}};
            if (kind == 4)
                row["wikiSubMetaVos"] = QJsonArray{QJsonObject{{"text", QString(4096, 'x')}}};
            if (kind == 5) {
                QJsonArray m;
                for (int i = 0; i < 16; ++i)
                    m.append(QJsonObject{{"text", ""}});
                row["wikiSubMetaVos"] = m;
                for (int i = 0; i < 100; ++i)
                    rows.append(row);
            } else
                rows.append(row);
        } else {
            for (int i = 0; i < 32; ++i)
                rows.append(QJsonObject{
                    {"title", ""},
                    {"content",
                     QString(i == 31 ? 4096 - q.sourceTitle().size() + (kind == 7 ? 1 : 0) : 4096,
                             'x')}});
        }
        info["wikiSubElementVos"] = rows;
        p["blocks"] =
            QJsonArray{QJsonObject{{"blockCode", "wikiSubBlockBaseInfoVo"}, {"blockInfo", info}}};
        main["rnData"] = p;
        outer[1] = main;
        data["blocks"] = outer;
        o["data"] = data;
        const auto result = mapMusicResponse(q, o);
        QCOMPARE(std::holds_alternative<SongWikiText>(result), kind != 7);
    }
    void factoryAndGroupBounds()
    {
        SessionCoordinator session;
        auto song = wikiTextSong();
        song.title = QString(4096, 'x');
        QVERIFY(SongWikiTextQuery::fromSong(session.snapshot().token, song));
        song.album->id->value = "01";
        QVERIFY(!SongWikiTextQuery::fromSong(session.snapshot().token, song));
        const auto q = wikiTextQuery(session.snapshot().token);
        auto o = wikiTextObject();
        auto data = o["data"].toObject();
        auto main = data["blocks"].toArray()[1].toObject();
        auto p = main["rnData"].toObject();
        QJsonArray groups;
        for (int i = 0; i < 32; ++i)
            groups.append(QJsonObject{{"blockCode", "syntheticUnknown"}});
        p["blocks"] = groups;
        main["rnData"] = p;
        QJsonArray outer{main};
        for (int i = 1; i < 32; ++i)
            outer.append(QJsonObject{{"positionCode", "syntheticUnknown"}});
        data["blocks"] = outer;
        o["data"] = data;
        auto r = mapMusicResponse(q, o);
        QVERIFY(std::holds_alternative<SongWikiText>(r));
        QCOMPARE(std::get<SongWikiText>(r).unsupportedBlockCount, 63);
    }
    void nativeChain()
    {
        Plan plan{wikiTextBody()};
        plan.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-untrusted; Path=/; Secure"});
        Harness h({plan});
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto q = wikiTextQuery(token);
        QVERIFY(h.service.request(token, q));
        QTRY_COMPARE(spy.size(), 1);
        QVERIFY(std::holds_alternative<SongWikiText>(last(spy)));
        QCOMPARE(h.http->requests.size(), 1);
        QCOMPARE(h.http->requests[0].url().host(), "interfacepc.music.163.com");
        QCOMPARE(h.http->requests[0].url().path(),
                 "/eapi/link/page/parent/relation/construct/info");
        const auto plain = decodeEapiRequest(h.http->bodies[0]);
        QVERIFY(plain);
        QCOMPARE(plain->logicalPath, "/api/link/page/parent/relation/construct/info");
        QCOMPARE(plain->payload.value("positionCode").toString(), "songWikiMainPosition");
        QVERIFY(!plain->payload["e_r"].toBool());
        const auto ext =
            QJsonDocument::fromJson(plain->payload["extJson"].toString().toUtf8()).object();
        QCOMPARE(ext["states"].toObject()["playingResource"].toObject()["current"].toString(),
                 q.song().value);
        QTest::qWait(10);
        QCOMPARE(h.http->requests.size(), 1);
        const auto source = h.service.request(token, MyPlaylistsQuery{"41", {}});
        QVERIFY(source);
        QTRY_COMPARE(spy.size(), 2);
        QCOMPARE(std::get<MusicFailure>(last(spy)).error, MusicError::AuthenticationRequired);
        QSignalSpy raw(&h.client, &SessionApiClient::completed);
        QVERIFY(h.client.submit(token, ApiOperation::MyPlaylists,
                                {{"uid", "41"}, {"limit", 30}, {"offset", 0}}));
        QTRY_COMPARE(raw.size(), 1);
        const auto rejected = qvariant_cast<ApiResult>(raw.last()[2]);
        QCOMPARE(rejected.error, ApiError::Cookie);
        QVERIFY(!rejected.submittedToTransport);
        QCOMPARE(h.http->requests.size(), 1);
    }
    void providerErrors_data()
    {
        QTest::addColumn<int>("code");
        for (int c : {301, 401, 403, 429, 500})
            QTest::newRow(qPrintable(QString::number(c))) << c;
    }
    void providerErrors()
    {
        QFETCH(int, code);
        Harness h({Plan{QJsonDocument(QJsonObject{{"code", code}}).toJson()}});
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token,
                                  wikiTextQuery(h.session.snapshot().token)));
        QTRY_COMPARE(spy.size(), 1);
        QVERIFY(std::holds_alternative<MusicFailure>(last(spy)));
        QCOMPARE(h.http->requests.size(), 1);
    }
    void scopedCancel_data()
    {
        QTest::addColumn<int>("mode");
        for (int m = 0; m < 4; ++m)
            QTest::newRow(qPrintable(QString::number(m))) << m;
    }
    void scopedCancel()
    {
        QFETCH(int, mode);
        Harness h({Plan{wikiTextBody(), 200, -1}});
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        const auto t = h.session.snapshot().token;
        const auto id = h.service.request(t, wikiTextQuery(t));
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
        QTest::qWait(10);
        QCOMPARE(spy.size(), 1);
    }
    void slotsAndStaleOrigin()
    {
        Harness h(QList<Plan>(16, Plan{"{}", 200, -1}));
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        const auto t = h.session.snapshot().token;
        const auto q = wikiTextQuery(t);
        for (int i = 0; i < 16; ++i)
            QVERIFY(h.service.request(t, q));
        QCOMPARE(h.service.request(t, q), 0);
        QTRY_COMPARE(h.http->requests.size(), 16);
        (void)h.session.beginAuthentication();
        QTRY_COMPARE(spy.size(), 16);
        for (const auto& event : spy)
            QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(event[2])).error,
                     MusicError::StaleContext);
        QCOMPARE(h.http->requests.size(), 16);
        Harness clean({});
        QSignalSpy isolated(&clean.service, &MusicQueryService::completed);
        const auto origin = wikiTextQuery(clean.session.snapshot().token);
        (void)clean.session.beginAuthentication();
        clean.session.signOut();
        QCOMPARE(clean.session.snapshot().phase, SessionPhase::Guest);
        QVERIFY(clean.service.request(clean.session.snapshot().token, origin));
        QTRY_COMPARE(isolated.size(), 1);
        QCOMPARE(std::get<MusicFailure>(last(isolated)).error, MusicError::StaleContext);
        QCOMPARE(clean.http->requests.size(), 0);
    }
    void facadeCorrelation()
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
        const auto q = wikiTextQuery(session.snapshot().token);
        for (int mode = 0; mode < 4; ++mode) {
            const auto id = f.songWikiText(q);
            QVERIFY(id);
            const auto c = qvariant_cast<MusicCommand>(requests.last()[0]);
            auto page = std::get<SongWikiText>(mapMusicResponse(q, wikiTextObject()));
            if (mode == 0)
                page.observedSongName = "different";
            if (mode == 1)
                page.observedAlbum.value = "41";
            if (mode == 2) {
                auto s = wikiTextSong();
                s.id.value = "41";
                page.query = *SongWikiTextQuery::fromSong(c.context, s);
            }
            if (mode == 3) {
                f.deliverMusic({id, c.generation, c.context, SongSearchPage{}});
            } else
                f.deliverMusic({id, c.generation, c.context, page});
            QCOMPARE(std::get<MusicFailure>(last(results)).error, MusicError::InvalidResponse);
        }
        QVERIFY(f.songWikiText(q));
        f.close();
        QTRY_COMPARE(results.size(), 5);
        QCOMPARE(f.songWikiText(q), 0);
        f.finishClose(false);
    }
};
QTEST_GUILESS_MAIN(SongWikiTextTest)
#include "song_wiki_text_test.moc"
