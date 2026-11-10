#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/catalog_mapper.h"
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
QJsonObject json(const QByteArray& body)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    Q_ASSERT(error.error == QJsonParseError::NoError && document.isObject());
    return document.object();
}
QList<MusicQuery> queries()
{
    return {DetailQuery{{SongId{"1"}}},
            AlbumQuery{{"2"}},
            ArtistQuery{{"3"}},
            PlaylistQuery{{"4"}},
            ArtistIntroductionQuery{{"3"}},
            SongWikiQuery{{"1"}}};
}
QJsonObject ui(const QString& title) { return {{"mainTitle", QJsonObject{{"title", title}}}}; }
QJsonObject creative(const QString& type)
{
    auto element = ui("synthetic label");
    element.insert("textLinks", QJsonArray{QJsonObject{{"text", "120"}, {"url", "ignored"}}});
    return {{"creativeType", type},
            {"uiElement", element},
            {"resources", QJsonArray{QJsonObject{{"uiElement", ui("synthetic tag")}}}}};
}
QJsonObject seed(int kind)
{
    const QList<QByteArray> bodies{
        R"({"code":200,"songs":[{"id":1,"name":"synthetic","alia":["alias","alias"],"tns":["translation"],"publishTime":-1,"dt":2000}]})",
        R"({"code":200,"album":{"id":2,"name":"synthetic","description":"synthetic album introduction","alias":["alias"],"company":"synthetic label","type":"EP","subType":"synthetic version","publishTime":-1,"size":1},"songs":[{"id":1}]})",
        R"({"code":200,"data":{"artist":{"id":3,"name":"synthetic","briefDesc":"synthetic brief","alias":["alias"],"transNames":["translation"]}}})",
        R"({"code":200,"playlist":{"id":4,"description":"synthetic playlist introduction","tags":["tag","tag"],"creator":{"userId":"9007199254740993","nickname":"synthetic"},"playCount":1.5,"createTime":0,"subscribedCount":0,"commentCount":2,"shareCount":3,"trackCount":1,"trackIds":[{"id":1}],"tracks":[]}})",
        R"({"code":200,"id":3,"briefDesc":"synthetic brief","introduction":[{"ti":"synthetic section","txt":"<b>synthetic plain text</b>"},{"txt":"synthetic second"}]})"};
    if (kind < 5)
        return json(bodies[kind]);
    return {{"code", 200},
            {"data",
             QJsonObject{
                 {"songId", 1},
                 {"blocks",
                  QJsonArray{QJsonObject{{"code", "SONG_PLAY_ABOUT_SONG_BASIC"},
                                         {"creatives",
                                          QJsonArray{creative("songTag"), creative("songBizTag"),
                                                     creative("language"), creative("bpm"),
                                                     creative("future")}}},
                             QJsonObject{{"code", "SONG_PLAY_ABOUT_SIMILAR_SONG"}}}}}}};
}
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last()[2]); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto manager = std::make_unique<FakeManager>();
        out = manager.get();
        manager->plans = std::move(plans);
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    explicit Harness(QList<Plan> plans, bool auth = false)
        : client(transport(std::move(plans), http), "synthetic-metadata")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto promotion = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(promotion);
        QVERIFY(session.completeAuthentication(*promotion));
        Plan p{R"({"code":200,"unikey":"synthetic"})"};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(p);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
};
} // namespace
class CatalogMetadataTest : public QObject
{
    Q_OBJECT
  private slots:
    void fourEntitiesRetainIndependentMetadata()
    {
        const auto song =
            std::get<SongDetails>(mapMusicResponse(queries()[0], seed(0))).songs.first();
        QCOMPARE(song.aliases, std::optional<QStringList>(QStringList{"alias", "alias"}));
        QCOMPARE(song.translatedTitles, std::optional<QStringList>(QStringList{"translation"}));
        QCOMPARE(song.publishedAtMs, std::optional<qint64>(-1));
        QCOMPARE(song.durationMs, std::optional<qint64>(2000));
        const auto album = std::get<AlbumDetails>(mapMusicResponse(queries()[1], seed(1)));
        QCOMPARE(album.album.description, "synthetic album introduction");
        QCOMPARE(album.album.company, std::optional<QString>("synthetic label"));
        QCOMPARE(album.album.type, std::optional<QString>("EP"));
        QCOMPARE(album.album.subType, std::optional<QString>("synthetic version"));
        QCOMPARE(album.album.aliases, std::optional<QStringList>(QStringList{"alias"}));
        QVERIFY(album.complete);
        const auto artist = std::get<ArtistDetails>(mapMusicResponse(queries()[2], seed(2)));
        QCOMPARE(artist.description, "synthetic brief");
        QCOMPARE(artist.translatedNames, std::optional<QStringList>(QStringList{"translation"}));
        const auto playlist = std::get<PlaylistDetails>(mapMusicResponse(queries()[3], seed(3)));
        QCOMPARE(playlist.description, "synthetic playlist introduction");
        QCOMPARE(playlist.tags, std::optional<QStringList>(QStringList{"tag", "tag"}));
        QCOMPARE(playlist.creator->publicUserId, std::optional<QString>("9007199254740993"));
        QCOMPARE(playlist.creator->nickname, "synthetic");
        QCOMPARE(playlist.playCount, std::optional<double>(1.5));
        QCOMPARE(playlist.subscriberCount, std::optional<qint64>(0));
        QCOMPARE(playlist.createdAtMs, std::optional<qint64>(0));
        QVERIFY(playlist.trackIdsComplete);
        QVERIFY(playlist.songs.isEmpty()); // introduction is NOT all songs or owner proof
    }
    void separateIntroductionAndWiki()
    {
        const auto intro = std::get<ArtistIntroduction>(mapMusicResponse(queries()[4], seed(4)));
        QCOMPARE(intro.artist.value, "3");
        QCOMPARE(intro.sections.size(), 2);
        QCOMPARE(intro.sections.first().text, "<b>synthetic plain text</b>");
        QVERIFY(!intro.sections.last().title);
        const auto wiki = std::get<SongWikiSummary>(mapMusicResponse(queries()[5], seed(5)));
        QCOMPARE(wiki.song.value, "1");
        QCOMPARE(wiki.cards.size(), 4);
        QVERIFY(wiki.unsupportedContent);
        QCOMPARE(wiki.cards[0].kind, SongWikiKind::Tags);
        QCOMPARE(wiki.cards[1].kind, SongWikiKind::BusinessTags);
        QCOMPARE(wiki.cards[2].kind, SongWikiKind::Language);
        QCOMPARE(wiki.cards[3].kind, SongWikiKind::Bpm);
        QCOMPARE(wiki.cards[3].values, QStringList{"120"});
        auto empty = json(R"({"code":200,"data":{"blocks":[]}})");
        const auto knownEmpty = std::get<SongWikiSummary>(mapMusicResponse(queries()[5], empty));
        QVERIFY(knownEmpty.cards.isEmpty());
        QVERIFY(!knownEmpty.unsupportedContent); // NOT a global encyclopedia completeness flag
        const auto noSections = std::get<ArtistIntroduction>(mapMusicResponse(
            queries()[4], json(R"({"code":200,"introduction":[],"briefDesc":""})")));
        QCOMPARE(noSections.briefDescription, std::optional<QString>(""));
        QVERIFY(noSections.sections.isEmpty());
    }
    void absentIsNotEmptyAndBudgets()
    {
        auto object = seed(0);
        auto song = object["songs"].toArray().first().toObject();
        song.remove("alia");
        song.insert("tns", QJsonArray{});
        song.insert("publishTime", QJsonValue());
        object["songs"] = QJsonArray{song};
        const auto value =
            std::get<SongDetails>(mapMusicResponse(queries()[0], object)).songs.first();
        QVERIFY(!value.aliases);
        QVERIFY(value.translatedTitles);
        QVERIFY(value.translatedTitles->isEmpty());
        QVERIFY(!value.publishedAtMs);
        song["alia"] = QJsonArray::fromStringList(QStringList(100, QString(4096, 'x')));
        object["songs"] = QJsonArray{song};
        QVERIFY(std::holds_alternative<SongDetails>(mapMusicResponse(queries()[0], object)));
        auto intro = seed(4);
        intro["introduction"] = QJsonArray{QJsonObject{{"txt", QString(65536, 'x')}}};
        QVERIFY(std::holds_alternative<ArtistIntroduction>(mapMusicResponse(queries()[4], intro)));
        intro["introduction"] = QJsonArray{QJsonObject{{"txt", QString(65537, 'x')}}};
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(queries()[4], intro)));
        auto many = QJsonArray{};
        for (int i = 0; i < 4; ++i)
            many.append(QJsonObject{{"txt", QString(65536, 'x')}});
        intro["briefDesc"] = "x";
        intro["introduction"] = many;
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(queries()[4], intro)));
        many = {};
        for (int i = 0; i < 64; ++i)
            many.append(QJsonObject{{"txt", ""}});
        intro["introduction"] = many;
        QVERIFY(std::holds_alternative<ArtistIntroduction>(mapMusicResponse(queries()[4], intro)));
        many.append(QJsonObject{{"txt", ""}});
        intro["introduction"] = many;
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(queries()[4], intro)));
    }
    void malformed_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QJsonObject>("object");
        const auto add = [](const char* name, int index, const QJsonObject& object) {
            QTest::newRow(name) << index << object;
        };
        for (int index = 0; index < 4; ++index) {
            const auto keys = QStringList{"alia", "alias", "alias", "tags"};
            for (const auto& [suffix, bad] : QList<std::pair<QString, QJsonValue>>{
                     {"object", QJsonObject{}},
                     {"null-item", QJsonArray{QJsonValue()}},
                     {"number-item", QJsonArray{1}},
                     {"long-item", QJsonArray{QString(4097, 'x')}},
                     {"many-items", QJsonArray::fromStringList(QStringList(101, "x"))},
                     {"nul-item", QJsonArray{QString(1, QChar::Null)}}}) {
                auto object = seed(index);
                QJsonObject entity;
                if (index == 0)
                    entity = object["songs"].toArray().first().toObject();
                else if (index == 1)
                    entity = object["album"].toObject();
                else if (index == 2)
                    entity = object["data"].toObject()["artist"].toObject();
                else
                    entity = object["playlist"].toObject();
                entity[keys[index]] = bad;
                if (index == 0)
                    object["songs"] = QJsonArray{entity};
                else if (index == 1)
                    object["album"] = entity;
                else if (index == 2)
                    object["data"] = QJsonObject{{"artist", entity}};
                else
                    object["playlist"] = entity;
                add(qPrintable(QString::number(index) + "-" + suffix), index, object);
            }
        }
        for (const auto& [name, body] : QList<std::pair<const char*, QByteArray>>{
                 {"intro-missing", R"({"code":200})"},
                 {"intro-null", R"({"code":200,"introduction":null})"},
                 {"intro-non-section", R"({"code":200,"introduction":[1]})"},
                 {"intro-no-text", R"({"code":200,"introduction":[{"ti":"x"}]})"},
                 {"intro-wrong-text", R"({"code":200,"introduction":[{"txt":1}]})"},
                 {"intro-wrong-title", R"({"code":200,"introduction":[{"txt":"x","ti":false}]})"},
                 {"intro-wrong-brief", R"({"code":200,"introduction":[],"briefDesc":[]})"},
                 {"intro-wrong-echo", R"({"code":200,"id":4,"introduction":[]})"}})
            add(name, 4, json(body));
        for (
            const auto& [name, body] : QList<std::pair<const char*, QByteArray>>{
                {"wiki-root-fallback", R"({"code":200,"blocks":[]})"},
                {"wiki-no-blocks", R"({"code":200,"data":{}})"},
                {"wiki-null-blocks", R"({"code":200,"data":{"blocks":null}})"},
                {"wiki-non-block", R"({"code":200,"data":{"blocks":[1]}})"},
                {"wiki-missing-code", R"({"code":200,"data":{"blocks":[{}]}})"},
                {"wiki-wrong-echo", R"({"code":200,"data":{"songId":2,"blocks":[]}})"},
                {"wiki-missing-creatives",
                 R"({"code":200,"data":{"blocks":[{"code":"SONG_PLAY_ABOUT_SONG_BASIC"}]}})"},
                {"wiki-non-creative",
                 R"({"code":200,"data":{"blocks":[{"code":"SONG_PLAY_ABOUT_SONG_BASIC","creatives":[null]}]}})"}})
            add(name, 5, json(body));
        for (const char* field : {"uiElement", "resources", "creativeType"}) {
            auto object = seed(5);
            auto data = object["data"].toObject();
            auto card = creative("songTag");
            card.remove(field);
            data["blocks"] = QJsonArray{QJsonObject{{"code", "SONG_PLAY_ABOUT_SONG_BASIC"},
                                                    {"creatives", QJsonArray{card}}}};
            object["data"] = data;
            add(field, 5, object);
        }
        for (const auto& [field, bad] : QList<std::pair<QString, QJsonValue>>{
                 {"createTime", -1},
                 {"subscribedCount", 1.5},
                 {"playCount", "2"},
                 {"creator", false},
                 {"creator", QJsonObject{{"userId", 9007199254740992.0}}}}) {
            auto object = seed(3);
            auto p = object["playlist"].toObject();
            p[field] = bad;
            object["playlist"] = p;
            add(qPrintable(field + QString::number(bad.type()) + bad.toVariant().toString()), 3,
                object);
        }
        for (const auto& [field, bad] :
             QList<std::pair<QString, QJsonValue>>{{"publishTime", 1.5},
                                                   {"publishTime", "1"},
                                                   {"publishTime", 9007199254740992.0},
                                                   {"tns", false}}) {
            auto object = seed(0);
            auto song = object["songs"].toArray().first().toObject();
            song[field] = bad;
            object["songs"] = QJsonArray{song};
            add(qPrintable(field + bad.toVariant().toString()), 0, object);
        }
        auto object = seed(1);
        auto album = object["album"].toObject();
        album["company"] = false;
        object["album"] = album;
        add("album-company", 1, object);
    }
    void malformed()
    {
        QFETCH(int, index);
        QFETCH(QJsonObject, object);
        const auto value = mapMusicResponse(queries()[index], object);
        QVERIFY(std::holds_alternative<MusicFailure>(value));
        QCOMPARE(std::get<MusicFailure>(value).error, MusicError::InvalidResponse);
    }
    void closedContracts()
    {
        for (const auto operation :
             {ApiOperation::ArtistIntroduction, ApiOperation::SongWikiSummary}) {
            const auto contract = operationContract(operation);
            QVERIFY(contract);
            QCOMPARE(contract->responseLimit, 2 * 1024 * 1024);
            QCOMPARE(contract->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(contract->destination, OfficialDestination::Default);
            QVERIFY(!requiresAccountCookie(operation));
            const QJsonObject id{{"id", "9007199254740993"}};
            const auto payload = operationPayload(operation, id);
            QVERIFY(payload);
            QCOMPARE(payload->size(), 1);
            QCOMPARE(payload->begin().value(), QJsonValue("9007199254740993"));
            QVERIFY(!operationPayload(operation, {{"id", 1}}));
            QVERIFY(!operationPayload(operation, {{"id", "01"}}));
            QVERIFY(!operationPayload(operation, {{"id", "1"}, {"header", QJsonObject{}}}));
            QVERIFY(!operationPayload(operation, {}));
        }
        QCOMPARE(operationContract(ApiOperation::ArtistIntroduction)->protocol, Protocol::Weapi);
        QCOMPARE(operationContract(ApiOperation::ArtistIntroduction)->logicalPath,
                 "/api/artist/introduction");
        QCOMPARE(operationContract(ApiOperation::SongWikiSummary)->protocol, Protocol::Eapi);
        QCOMPARE(operationContract(ApiOperation::SongWikiSummary)->logicalPath,
                 "/api/song/play/about/block/page");
        QVERIFY(!validMusicQuery(ArtistIntroductionQuery{{"0"}}));
        QVERIFY(!validMusicQuery(SongWikiQuery{{"../1"}}));
    }
    void native_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<bool>("auth");
        for (int index : {4, 5})
            for (bool auth : {false, true})
                QTest::newRow(qPrintable(QString::number(index) + (auth ? "-account" : "-guest")))
                    << index << auth;
    }
    void native()
    {
        QFETCH(int, index);
        QFETCH(bool, auth);
        Plan p{QJsonDocument(seed(index)).toJson(QJsonDocument::Compact)};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({p, p}, auth);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        for (int i = 1; i <= 2; ++i) {
            QVERIFY(h.service.request(token, queries()[index]));
            QTRY_COMPARE(done.size(), i);
            QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            const auto cookie = h.http->requests.last().rawHeader("Cookie");
            QVERIFY(!cookie.contains("synthetic-rotation"));
            QCOMPARE(cookie.contains("MUSIC_U=synthetic-original"), auth);
            const auto request = *catalogRequest(queries()[index]);
            const auto contract = *operationContract(request.operation);
            QCOMPARE(h.http->requests.last().url().path(),
                     (index == 4 ? "/weapi" : "/eapi") + contract.logicalPath.mid(4));
            if (index == 5) {
                const auto wire = decodeEapiRequest(h.http->bodies.last());
                QVERIFY(wire);
                QCOMPARE(wire->logicalPath, contract.logicalPath);
                auto payload = wire->payload;
                QVERIFY(payload["header"].isObject());
                QCOMPARE(payload.take("e_r"), QJsonValue(false));
                payload.remove("header");
                QCOMPARE(payload, QJsonObject({{"songId", "1"}}));
            } else {
                const QUrlQuery form(QString::fromLatin1(h.http->bodies.last()));
                QVERIFY(form.hasQueryItem("params"));
                QVERIFY(form.hasQueryItem("encSecKey"));
            }
        }
        QCOMPARE(h.http->requests.size(), auth ? 3 : 2);
    }
    void lifecycleAndFailures_data()
    {
        QTest::addColumn<int>("index");
        QTest::newRow("artist") << 4;
        QTest::newRow("wiki") << 5;
    }
    void lifecycleAndFailures()
    {
        QFETCH(int, index);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, queries()[index]);
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 1);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(token, queries()[index]));
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        Harness rate({Plan{"", 429}});
        QSignalSpy rd(&rate.service, &MusicQueryService::completed);
        QVERIFY(rate.service.request(rate.session.snapshot().token, queries()[index]));
        QTRY_COMPARE(rd.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(rd)).retryAfterMs, std::optional<qint64>(3000));
        QCOMPARE(std::get<MusicFailure>(result(rd)).error, MusicError::RateLimited);
        QTest::qWait(10);
        QCOMPARE(rate.http->requests.size(), 1);
        Harness rejected({Plan{R"({"code":-462})"}});
        QSignalSpy reject(&rejected.service, &MusicQueryService::completed);
        QVERIFY(rejected.service.request(rejected.session.snapshot().token, queries()[index]));
        QTRY_COMPARE(reject.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(reject)).providerCode, std::optional<int>(-462));
        QCOMPARE(std::get<MusicFailure>(result(reject)).error, MusicError::ProviderRejected);
    }
    void facadeBarrierAndNoHiddenActions()
    {
        BackendFacade f;
        QVERIFY(!f.artistIntroduction({"3"}));
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        const SessionToken token{QUuid::createUuid(), 1};
        e.session = {token, SessionPhase::Guest};
        f.deliver(e);
        QSignalSpy requests(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            reports(&f, &BackendFacade::listeningReportRequested),
            writes(&f, &BackendFacade::collectionRequested);
        QVERIFY(!f.songWikiSummary({"0"}));
        QVERIFY(!f.artistIntroduction({"01"}));
        QVERIFY(f.artistIntroduction({"3"}));
        QVERIFY(f.songWikiSummary({"1"}));
        for (int i = 0; i < 14; ++i)
            QVERIFY(f.songWikiSummary({"1"}));
        QVERIFY(!f.songWikiSummary({"1"}));
        const auto old = qvariant_cast<MusicCommand>(requests.first()[0]);
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 16);
        f.deliverMusic({old.id, old.generation, token, ArtistIntroduction{{"3"}}});
        QCOMPARE(done.size(), 16);
        QVERIFY(play.isEmpty());
        QVERIFY(reports.isEmpty());
        QVERIFY(writes.isEmpty());
    }
};
QTEST_GUILESS_MAIN(CatalogMetadataTest)
#include "catalog_metadata_test.moc"
