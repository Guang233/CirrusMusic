#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
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
QJsonObject json(QByteArray value) { return QJsonDocument::fromJson(value).object(); }
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last().at(2)); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& http)
    {
        auto m = std::make_unique<FakeManager>();
        http = m.get();
        m->plans = std::move(plans);
        return std::make_unique<HttpTransport>(std::move(m));
    }
    explicit Harness(QList<Plan> plans)
        : client(transport(std::move(plans), http), "synthetic-catalog")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
    }
};
} // namespace
class CatalogTest : public QObject
{
    Q_OBJECT
  private slots:
    void closedRoutesAndPayloads()
    {
        const QString wide = "9007199254740993";
        const QJsonObject id{{"id", wide}};
        const auto album = operationContract(ApiOperation::AlbumDetails, id);
        QVERIFY(album);
        QCOMPARE(album->logicalPath, "/api/v1/album/" + wide);
        QCOMPARE(album->protocol, Protocol::Weapi);
        QVERIFY(operationPayload(ApiOperation::AlbumDetails, id)->isEmpty());
        const QJsonObject paged{{"id", wide}, {"limit", 20}, {"offset", 40}};
        QCOMPARE(operationContract(ApiOperation::ArtistAlbums, paged)->logicalPath,
                 "/api/artist/albums/" + wide);
        QCOMPARE(*operationPayload(ApiOperation::ArtistAlbums, paged),
                 json(R"({"limit":20,"offset":40,"total":true})"));
        auto p = paged;
        p.insert("order", "time");
        const auto payload = operationPayload(ApiOperation::ArtistSongs, p);
        QVERIFY(payload);
        QCOMPARE(payload->value("private_cloud"), QJsonValue("true"));
        QCOMPARE(payload->value("id"), QJsonValue(wide));
        QCOMPARE(payload->value("order"), QJsonValue("time"));
        QCOMPARE(payload->value("work_type"), QJsonValue(1));
        QCOMPARE(operationPayload(ApiOperation::PlaylistDetails, id)->value("n"),
                 QJsonValue(100000));
        QVERIFY(!operationContract(ApiOperation::AlbumDetails));
        QVERIFY(!operationContract(ApiOperation::AlbumDetails, {{"id", "../login"}}));
        p.insert("order", "random");
        QVERIFY(!operationPayload(ApiOperation::ArtistSongs, p));
        p.insert("order", "hot");
        p.insert("limit", 1.5);
        QVERIFY(!operationPayload(ApiOperation::ArtistSongs, p));
        p = id;
        p.insert("host", "https://example.invalid");
        QVERIFY(!operationPayload(ApiOperation::PlaylistDetails, p));
        QVERIFY(!validMusicQuery(ArtistSongsQuery{{"1"}, {101, 0}}));
        QVERIFY(!validMusicQuery(ArtistAlbumsQuery{{"1"}, {20, -1}}));
        QVERIFY(!validMusicQuery(PlaylistSongsQuery{{"0"}}));
    }
    void albumDatesWideIdsAndCompleteness()
    {
        const AlbumQuery query{{"9007199254740993"}};
        auto o = json(
            R"({"code":200,"album":{"id":"9007199254740993","name":"fixture","size":2,"publishTime":-315619200000,"artist":{"id":0,"name":"unknown"}},"songs":[{"id":2}]})");
        auto r = mapMusicResponse(query, o);
        QVERIFY(std::holds_alternative<AlbumDetails>(r));
        auto a = std::get<AlbumDetails>(r);
        QVERIFY(!a.complete);
        QCOMPARE(a.album.id.value, query.album.value);
        QCOMPARE(a.album.publishedAtMs, std::optional<qint64>(-315619200000LL));
        QVERIFY(!a.album.artists.first().id);
        o.insert("songs", QJsonArray{QJsonObject{{"id", 2}}, QJsonObject{{"id", 3}}});
        QVERIFY(std::get<AlbumDetails>(mapMusicResponse(query, o)).complete);
        auto meta = o.value("album").toObject();
        meta.remove("size");
        o.insert("album", meta);
        QVERIFY(!std::get<AlbumDetails>(mapMusicResponse(query, o)).complete);
        meta.insert("id", qint64(9007199254740993LL));
        o.insert("album", meta);
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(query, o)));
    }
    void artistSchemasAndUnknownPagination()
    {
        // JSON-escaped slashes avoid moc's inline raw-string/comment scanner bug.
        auto r = mapMusicResponse(
            ArtistQuery{{"1"}},
            json(
                R"({"code":200,"data":{"artist":{"id":1,"name":"a","albumSize":3,"musicSize":4,"cover":"file:\/\/\/tmp/no"},"user":{"ignored":true}}})"));
        QVERIFY(std::holds_alternative<ArtistDetails>(r));
        const auto a = std::get<ArtistDetails>(r);
        QCOMPARE(a.songCount, std::optional<qint64>(4));
        QVERIFY(a.portrait.isEmpty());
        QVERIFY(!a.userId); // data.user without a userId: no account
        // A musician's account (data.user.userId); a malformed one rejects the page.
        const auto musician = std::get<ArtistDetails>(mapMusicResponse(
            ArtistQuery{{"1"}},
            json(
                R"({"code":200,"data":{"artist":{"id":1,"name":"a"},"user":{"userId":37179178}}})")));
        QCOMPARE(musician.userId, std::optional<QString>("37179178"));
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            ArtistQuery{{"1"}},
            json(R"({"code":200,"data":{"artist":{"id":1,"name":"a"},"user":{"userId":"x"}}})"))));
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            ArtistQuery{{"1"}},
            json(R"({"code":200,"data":{"artist":{"id":1,"name":"a"},"user":7}})"))));
        const auto songs = std::get<ArtistSongsPage>(
            mapMusicResponse(ArtistSongsQuery{{"1"}, {2, 10}}, json(R"({"code":200,"songs":[]})")));
        QVERIFY(!songs.hasMore);
        QVERIFY(!songs.total);
        QCOMPARE(songs.query.page.offset, 10);
        const auto albums = std::get<ArtistAlbumsPage>(mapMusicResponse(
            ArtistAlbumsQuery{{"1"}, {2, 0}},
            json(
                R"({"code":200,"artist":{"id":1,"albumSize":3},"hotAlbums":[{"id":9,"size":0}],"more":true})")));
        QCOMPARE(albums.total, std::optional<qint64>(3));
        QCOMPARE(albums.hasMore, std::optional<bool>(true));
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            ArtistAlbumsQuery{{"1"}}, json(R"({"code":200,"artist":{"id":2},"hotAlbums":[]})"))));
    }
    void playlistIndexIsNotEmbeddedTrackList()
    {
        const auto p = std::get<PlaylistDetails>(mapMusicResponse(
            PlaylistQuery{{"1"}},
            json(
                R"({"code":200,"playlist":{"id":1,"trackCount":3,"trackIds":[{"id":3},{"id":2},{"id":3}],"tracks":[{"id":3}]}})")));
        QVERIFY(p.trackIdsComplete);
        QCOMPARE(p.trackIds->size(), 3);
        QCOMPARE(p.songs.size(), 1);
        QCOMPARE(p.trackIds->first().value, p.trackIds->last().value);
        const auto missing = std::get<PlaylistDetails>(
            mapMusicResponse(PlaylistQuery{{"1"}}, json(R"({"code":200,"playlist":{"id":1}})")));
        QVERIFY(!missing.trackIds);
        QVERIFY(!missing.trackIdsComplete);
        QVERIFY(!missing.trackCount);
        const auto partial = std::get<PlaylistDetails>(mapMusicResponse(
            PlaylistQuery{{"1"}},
            json(R"({"code":200,"playlist":{"id":1,"trackCount":2,"trackIds":[{"id":3}]}})")));
        QVERIFY(!partial.trackIdsComplete);
    }
    void unreadableEmbeddedTrackLeavesPlaylist() // GitHub #4
    {
        // A liked-songs playlist whose embedded rows include one the song mapper will not
        // take (here: a NUL in tag text, a negative duration, a non-object row). The index
        // is fine, so the playlist loads; those songs are left to be asked for by ID.
        const auto p = std::get<PlaylistDetails>(mapMusicResponse(
            PlaylistQuery{{"1"}},
            json(
                R"({"code":200,"playlist":{"id":1,"trackCount":5,"trackIds":[{"id":2},{"id":3},{"id":4},{"id":5},{"id":6}],"tracks":[{"id":2,"name":"ok"},{"id":3,"name":"a\u0000b"},null,{"id":5,"dt":-1},{"id":6,"ar":[{"id":0,"name":"x"}]}]}})")));
        QVERIFY(p.trackIdsComplete);
        QCOMPARE(p.trackIds->size(), 5);
        QCOMPARE(p.songs.size(), 2);
        QCOMPARE(p.songs.first().id.value, QStringLiteral("2"));
        QCOMPARE(p.songs.last().id.value, QStringLiteral("6"));
        // Still a contradiction, not an unreadable row: an embedded song outside the index.
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            PlaylistQuery{{"1"}},
            json(
                R"({"code":200,"playlist":{"id":1,"trackIds":[{"id":2}],"tracks":[{"id":3,"name":"a\u0000b"},{"id":4}]}})"))));
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            PlaylistQuery{{"1"}}, json(R"({"code":200,"playlist":{"id":1,"tracks":{}}})"))));
    }
    void malformedCatalog_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("wrong-playlist") << 0 << QByteArray(R"({"code":200,"playlist":{"id":2}})");
        QTest::newRow("unsafe-track")
            << 0
            << QByteArray(
                   R"({"code":200,"playlist":{"id":1,"trackIds":[{"id":9007199254740993}]}})");
        QTest::newRow("malformed-index")
            << 0 << QByteArray(R"({"code":200,"playlist":{"id":1,"trackIds":{}}})");
        QTest::newRow("count-contradiction")
            << 0
            << QByteArray(
                   R"({"code":200,"playlist":{"id":1,"trackCount":0,"trackIds":[{"id":1}]}})");
        QTest::newRow("unsolicited-embedded")
            << 0
            << QByteArray(R"({"code":200,"playlist":{"id":1,"trackIds":[],"tracks":[{"id":1}]}})");
        QTest::newRow("bad-count")
            << 0 << QByteArray(R"({"code":200,"playlist":{"id":1,"trackCount":"1"}})");
        QTest::newRow("wrong-album")
            << 1 << QByteArray(R"({"code":200,"album":{"id":2},"songs":[]})");
        QTest::newRow("missing-songs") << 1 << QByteArray(R"({"code":200,"album":{"id":1}})");
        QTest::newRow("bad-artist-shape") << 2 << QByteArray(R"({"code":200,"artist":{"id":1}})");
        QTest::newRow("not-bool-more") << 3 << QByteArray(R"({"code":200,"songs":[],"more":1})");
        QTest::newRow("non-song") << 3 << QByteArray(R"({"code":200,"songs":[null]})");
    }
    void malformedCatalog()
    {
        QFETCH(int, kind);
        QFETCH(QByteArray, body);
        const QList<MusicQuery> queries{PlaylistQuery{{"1"}}, AlbumQuery{{"1"}}, ArtistQuery{{"1"}},
                                        ArtistSongsQuery{{"1"}}};
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(queries[kind], json(body))));
    }
    void nativeReadOperations()
    {
        struct Case {
            MusicQuery query;
            QByteArray body;
            QString path;
            size_t expected;
        };
        const QList<Case> cases{
            {AlbumQuery{{"9007199254740993"}},
             R"({"code":200,"album":{"id":"9007199254740993","size":0},"songs":[]})",
             "/weapi/v1/album/9007199254740993", MusicResult(AlbumDetails{}).index()},
            {ArtistQuery{{"1"}}, R"({"code":200,"data":{"artist":{"id":1}}})",
             "/eapi/artist/head/info/get", MusicResult(ArtistDetails{}).index()},
            {ArtistSongsQuery{{"1"}, {2, 20}, ArtistSongOrder::Time},
             R"({"code":200,"songs":[],"more":false})", "/eapi/v1/artist/songs",
             MusicResult(ArtistSongsPage{}).index()},
            {ArtistAlbumsQuery{{"9007199254740993"}, {2, 0}},
             R"({"code":200,"hotAlbums":[],"more":false})", "/weapi/artist/albums/9007199254740993",
             MusicResult(ArtistAlbumsPage{}).index()},
            {PlaylistQuery{{"1"}},
             R"({"code":200,"playlist":{"id":1,"trackCount":0,"trackIds":[]}})",
             "/eapi/v6/playlist/detail", MusicResult(PlaylistDetails{}).index()}};
        QList<Plan> plans;
        for (const auto& c : cases) {
            Plan p{c.body};
            p.headers.append(std::pair<QByteArray, QByteArray>{
                "Set-Cookie", "MUSIC_U=forged; Domain=music.163.com; Path=/"});
            plans.append(p);
        }
        Harness h(plans);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        for (int i = 0; i < cases.size(); ++i) {
            QVERIFY(h.service.request(h.session.snapshot().token, cases[i].query));
            QTRY_COMPARE(done.size(), i + 1);
            QCOMPARE(result(done).index(), cases[i].expected);
            QCOMPARE(h.http->requests.last().url().path(), cases[i].path);
            QVERIFY(!h.http->requests.last().rawHeader("Cookie").contains("forged"));
        }
        const auto wire = decodeEapiRequest(h.http->bodies[2]);
        QVERIFY(wire);
        QCOMPARE(wire->payload.value("offset"), QJsonValue(20));
        QCOMPARE(wire->payload.value("order"), QJsonValue("time"));
    }
    void playlistPageHydratesOnlySliceAndPreservesMissingDuplicates()
    {
        Harness h(
            {Plan{
                 R"({"code":200,"playlist":{"id":1,"updateTime":99,"trackCount":5,"trackIds":[{"id":9},{"id":"9007199254740993"},{"id":2},{"id":"9007199254740993"},{"id":8}],"tracks":[]}})"},
             Plan{R"({"code":200,"songs":[{"id":"9007199254740993","name":"wide"}]})"}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, PlaylistSongsQuery{{"1"}, {3, 1}}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(std::holds_alternative<PlaylistSongsPage>(result(done)));
        const auto p = std::get<PlaylistSongsPage>(result(done));
        QCOMPARE(p.ids.size(), 3);
        QCOMPARE(p.songs.size(), 2);
        QCOMPARE(p.missing.size(), 1);
        QCOMPARE(p.missing[0].value, "2");
        QCOMPARE(p.songs[0].id, p.songs[1].id);
        QCOMPARE(p.total, 5);
        QVERIFY(p.hasMore);
        QCOMPARE(p.indexUpdatedAtMs, std::optional<qint64>(99));
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(h.http->requests.last().url().path(), "/weapi/v3/song/detail");
    }
    void incompleteAndEmptyIndexDoNotFetchDetails()
    {
        const QList<QByteArray> bodies{
            R"({"code":200,"playlist":{"id":1,"trackCount":2,"trackIds":[{"id":3}]}})",
            R"({"code":200,"playlist":{"id":1,"trackIds":[]}})",
            R"({"code":200,"playlist":{"id":1,"trackCount":0,"trackIds":[]}})",
            R"({"code":200,"playlist":{"id":1,"trackCount":1,"trackIds":[{"id":3}]}})"};
        for (int i = 0; i < bodies.size(); ++i) {
            Harness h({Plan{bodies[i]}});
            QSignalSpy done(&h.service, &MusicQueryService::completed);
            QVERIFY(h.service.request(h.session.snapshot().token,
                                      PlaylistSongsQuery{{"1"}, {10, 100}}));
            QTRY_COMPARE(done.size(), 1);
            if (i < 2)
                QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::IncompleteCatalog);
            else {
                const auto p = std::get<PlaylistSongsPage>(result(done));
                QVERIFY(p.ids.isEmpty());
                QVERIFY(!p.hasMore);
            }
            QCOMPARE(h.http->requests.size(), 1);
        }
    }
    void cancelAndContextAcrossBothStages()
    {
        const QByteArray index =
            R"({"code":200,"playlist":{"id":1,"trackCount":1,"trackIds":[{"id":2}]}})";
        for (bool second : {false, true})
            for (bool logout : {false, true}) {
                Harness h(second ? QList<Plan>{Plan{index}, Plan{"{}", 200, -1}}
                                 : QList<Plan>{Plan{index, 200, -1}});
                QSignalSpy done(&h.service, &MusicQueryService::completed);
                const auto context = h.session.snapshot().token;
                const auto id = h.service.request(context, PlaylistSongsQuery{{"1"}});
                QVERIFY(id);
                QTRY_COMPARE(h.http->requests.size(), second ? 2 : 1);
                if (logout)
                    h.session.signOut();
                else
                    h.service.cancel(id);
                QTRY_COMPARE(done.size(), 1);
                QCOMPARE(qvariant_cast<SessionToken>(done[0][1]), context);
                QCOMPARE(std::get<MusicFailure>(result(done)).error,
                         logout ? MusicError::StaleContext : MusicError::Canceled);
                QCoreApplication::processEvents();
                QCOMPARE(done.size(), 1);
                QCOMPARE(h.http->requests.size(), second ? 2 : 1);
            }
    }
    void secondStageFailureIsNotPartialSuccess()
    {
        Harness h({Plan{R"({"code":200,"playlist":{"id":1,"trackCount":1,"trackIds":[{"id":2}]}})"},
                   Plan{"{}", 429}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, PlaylistSongsQuery{{"1"}}));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::RateLimited);
        QCOMPARE(h.http->requests.size(), 2);
    }
};
QTEST_GUILESS_MAIN(CatalogTest)
#include "catalog_test.moc"
