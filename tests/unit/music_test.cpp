#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/lyric_parser.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;
using namespace cirrus::netease;
using namespace cirrus::test;

static QJsonObject object(const QByteArray& json) { return QJsonDocument::fromJson(json).object(); }
static QJsonObject lyric(const QString& text) { return {{QStringLiteral("lyric"), text}}; }
static MusicResult resultAt(const QSignalSpy& spy, int index = 0)
{
    return qvariant_cast<MusicResult>(spy.at(index).at(2));
}

class MusicTest : public QObject
{
    Q_OBJECT
  private slots:
    void lineLyrics();
    void wordLyrics();
    void allLanguageVersionsStayIndependent();
    void absentEmptyAndInstrumentalDiffer();
    void malformedLyrics_data();
    void malformedLyrics();
    void metadataShapesAndWideIds();
    void detailOrderAndMissing();
    void emptyAndUnknownPagination();
    void invalidMetadata_data();
    void invalidMetadata();
    void closedOperationPayloads();
    void nativeQueryChain();
    void cancellationAndSessionChange();
    void rejectsDuringAuthentication();
};

void MusicTest::lineLyrics()
{
    const auto track =
        parseLyricText(QStringLiteral("\ufeff[ar:fixture]\r\n[offset:250]\n[00:02.5][00:01.05]A\n["
                                      "00:01.050]B\n[00:03.001]\nplain"));
    QCOMPARE(track.availability, LyricAvailability::Present);
    QCOMPARE(track.timing, LyricTiming::Lines);
    QCOMPARE(track.providerOffsetMs, -250);
    QCOMPARE(track.lines.size(), 5);
    QCOMPARE(track.lines.at(0).startMs, std::optional<qint64>(1050));
    QCOMPARE(track.lines.at(0).text, QStringLiteral("A"));
    QCOMPARE(track.lines.at(1).text, QStringLiteral("B")); // same-time duet preserved, stable order
    QCOMPARE(track.lines.at(0).endMs, std::optional<qint64>(2500));
    QCOMPARE(track.lines.at(1).endMs, std::optional<qint64>(2500));
    QVERIFY(!track.lines.at(3).endMs); // final timed empty line has unknown end
    QVERIFY(track.lines.at(3).text.isEmpty());
    QVERIFY(!track.lines.last().startMs); // never invent timing for plain text
}

void MusicTest::wordLyrics()
{
    const auto track = parseLyricText(QStringLiteral(
        "{\"t\":0,\"c\":[{\"tx\":\"credit\",\"or\":\"ignored://"
        "link\"}]}\n[1000,900](1000,500,0)你(1400,500,1)好\n[1800,100](1800,100,0)\n"));
    QCOMPARE(track.timing, LyricTiming::Words);
    QCOMPARE(track.credits, QStringList{QStringLiteral("credit")});
    QCOMPARE(track.lines.size(), 2);
    QCOMPARE(track.lines.first().endMs, std::optional<qint64>(1900));
    QCOMPARE(track.lines.first().text, QStringLiteral("你好"));
    QCOMPARE(track.lines.first().words.at(1).startMs, 1400);   // absolute, not line-relative
    QCOMPARE(track.lines.first().words.at(0).durationMs, 500); // overlap preserved
    QVERIFY(track.lines.last().words.first().text.isEmpty());
}

void MusicTest::allLanguageVersionsStayIndependent()
{
    QJsonObject data{{QStringLiteral("code"), 200}};
    data.insert(QStringLiteral("lrc"), lyric(QStringLiteral("[00:01]source\n[00:02]next")));
    data.insert(QStringLiteral("yrc"), lyric(QStringLiteral("[1000,400](1000,400,0)source")));
    data.insert(QStringLiteral("tlyric"), lyric(QStringLiteral("[00:01.2]translation")));
    data.insert(QStringLiteral("ytlrc"), lyric(QStringLiteral("[00:01.2]enhanced translation")));
    data.insert(QStringLiteral("romalrc"), lyric(QStringLiteral("[00:01.3]roman")));
    data.insert(QStringLiteral("yromalrc"), lyric(QStringLiteral("[1300,300](1300,300,0)roman")));
    const auto result = mapMusicResponse(LyricQuery{{QStringLiteral("101")}}, data);
    QVERIFY(std::holds_alternative<SongLyrics>(result));
    const auto document = std::get<SongLyrics>(result);
    QCOMPARE(document.original.standard.lines.size(), 2);
    QCOMPARE(document.original.enhanced.timing, LyricTiming::Words);
    QCOMPARE(document.translation.enhanced.timing,
             LyricTiming::Lines); // y-prefix isn't a word-format guarantee
    QCOMPARE(document.romanization.enhanced.timing, LyricTiming::Words);
    QCOMPARE(document.translation.standard.lines.first().startMs, std::optional<qint64>(1200));
    QCOMPARE(document.romanization.standard.lines.first().startMs, std::optional<qint64>(1300));
}

void MusicTest::absentEmptyAndInstrumentalDiffer()
{
    const auto missing = std::get<SongLyrics>(
        mapMusicResponse(LyricQuery{{QStringLiteral("101")}},
                         object(R"({"code":200,"lrc":{"lyric":""},"nolyric":true})")));
    QCOMPARE(missing.original.standard.availability, LyricAvailability::Empty);
    QCOMPARE(missing.original.enhanced.availability, LyricAvailability::Missing);
    QVERIFY(!missing.instrumental.has_value());
    QCOMPARE(missing.providerNoLyric, std::optional<bool>(true));
    const auto pure = std::get<SongLyrics>(
        mapMusicResponse(LyricQuery{{QStringLiteral("101")}},
                         object(R"({"code":200,"pureMusic":true,"yrc":{"lyric":12}})")));
    QCOMPARE(pure.instrumental, std::optional<bool>(true));
    QCOMPARE(pure.original.enhanced.availability, LyricAvailability::Malformed);
}

void MusicTest::malformedLyrics_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("bad-seconds") << QStringLiteral("[00:99]bad");
    QTest::newRow("word-before-line") << QStringLiteral("[1000,100](900,100,0)bad");
    QTest::newRow("overflow") << QStringLiteral("[999999999999,100](999999999999,100,0)bad");
    QTest::newRow("unknown-word-format") << QStringLiteral("[1000,100]untimed content");
    QTest::newRow("oversize") << QString(512 * 1024 + 1, u'a');
    QTest::newRow("nul") << (QStringLiteral("plain") + QChar::Null);
}
void MusicTest::malformedLyrics()
{
    QFETCH(QString, text);
    const auto result = parseLyricText(text);
    QCOMPARE(result.availability, LyricAvailability::Malformed);
    QVERIFY(result.lines.isEmpty());
}

void MusicTest::metadataShapesAndWideIds()
{
    const auto query = SongSearch{QStringLiteral("fixture")};
    const auto result = mapMusicResponse(
        query,
        object(
            R"({"code":200,"result":{"songCount":2,"hasMore":false,"songs":[{"id":"9007199254740993","name":"wide","artists":[{"id":3,"name":"a"}],"album":{"id":4,"name":"b"},"duration":120000},{"id":5,"name":"native","ar":[{"id":0,"name":"unknown"}],"al":null}]}})"));
    QVERIFY(std::holds_alternative<SongSearchPage>(result));
    const auto page = std::get<SongSearchPage>(result);
    QCOMPARE(page.songs.first().id.value, QStringLiteral("9007199254740993"));
    QCOMPARE(page.songs.first().durationMs, std::optional<qint64>(120000));
    QVERIFY(!page.songs.last().durationMs);
    QVERIFY(!page.songs.last().artists.first().id);
    QCOMPARE(page.hasMore, std::optional<bool>(false));
}

void MusicTest::detailOrderAndMissing()
{
    const DetailQuery query{{{QStringLiteral("2")}, {QStringLiteral("3")}, {QStringLiteral("1")}}};
    const auto result = mapMusicResponse(
        query, object(R"({"code":200,"songs":[{"id":1,"name":"a"},{"id":2,"name":"b"}]})"));
    QVERIFY(std::holds_alternative<SongDetails>(result));
    const auto details = std::get<SongDetails>(result);
    QCOMPARE(details.songs.at(0).id.value, QStringLiteral("2"));
    QCOMPARE(details.songs.at(1).id.value, QStringLiteral("1"));
    QCOMPARE(details.missing.size(), 1);
    QCOMPARE(details.missing.first().value, QStringLiteral("3"));
}

void MusicTest::emptyAndUnknownPagination()
{
    const SongSearch query{QStringLiteral("fixture")};
    const auto empty = mapMusicResponse(query, object(R"({"code":200,"result":{"songCount":0}})"));
    QVERIFY(std::holds_alternative<SongSearchPage>(empty));
    const auto unknown = std::get<SongSearchPage>(
        mapMusicResponse(query, object(R"({"code":200,"result":{"songs":[]}})")));
    QVERIFY(!unknown.total);
    QVERIFY(!unknown.hasMore);
}

void MusicTest::invalidMetadata_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("unsafe-number")
        << QByteArray(R"({"code":200,"songs":[{"id":9007199254740993}]})");
    QTest::newRow("fractional") << QByteArray(R"({"code":200,"songs":[{"id":1.5}]})");
    QTest::newRow("noncanonical") << QByteArray(R"({"code":200,"songs":[{"id":"01"}]})");
    QTest::newRow("duplicate") << QByteArray(R"({"code":200,"songs":[{"id":1},{"id":1}]})");
    QTest::newRow("unsolicited") << QByteArray(R"({"code":200,"songs":[{"id":2}]})");
    QTest::newRow("duration") << QByteArray(R"({"code":200,"songs":[{"id":1,"dt":-3}]})");
    QTest::newRow("wrapped") << QByteArray(R"({"data":{"code":200,"songs":[{"id":1}]}})");
}
void MusicTest::invalidMetadata()
{
    QFETCH(QByteArray, json);
    QVERIFY(std::holds_alternative<MusicFailure>(
        mapMusicResponse(DetailQuery{{{QStringLiteral("1")}}}, object(json))));
}

void MusicTest::closedOperationPayloads()
{
    const auto details =
        operationPayload(ApiOperation::SongDetails,
                         {{QStringLiteral("ids"), QJsonArray{QStringLiteral("9007199254740993")}}});
    QVERIFY(details);
    QCOMPARE(details->value(QStringLiteral("c")).toString(),
             QStringLiteral("[{\"id\":9007199254740993}]"));
    const auto words =
        operationPayload(ApiOperation::SongLyrics, {{QStringLiteral("id"), QStringLiteral("101")}});
    QVERIFY(words);
    QCOMPARE(words->size(), 9);
    QVERIFY(words->contains(QStringLiteral("yrv")));
    QVERIFY(!operationPayload(ApiOperation::SearchSongs,
                              {{QStringLiteral("keywords"), QStringLiteral("a")},
                               {QStringLiteral("limit"), 1.5},
                               {QStringLiteral("offset"), 0}}));
    QVERIFY(!operationPayload(ApiOperation::SongLyrics,
                              {{QStringLiteral("id"), QStringLiteral("1,2")}}));
}

void MusicTest::nativeQueryChain()
{
    SessionCoordinator session;
    auto manager = std::make_unique<FakeManager>();
    auto* http = manager.get();
    http->plans = {Plan{R"({"code":200,"result":{"songCount":0,"songs":[]}})"},
                   Plan{R"({"code":200,"songs":[{"id":1,"name":"test"}]})"},
                   Plan{R"({"code":200,"yrc":{"lyric":"[1000,100](1000,100,0)x"}})"},
                   Plan{"{}", 429}};
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    QVERIFY(client.advanceTo(session.snapshot().token));
    client.setAnonymousToken("synthetic-anon");
    MusicGateway gateway(client);
    MusicQueryService service(session, gateway);
    QSignalSpy done(&service, &MusicQueryService::completed);
    const auto token = session.snapshot().token;
    QVERIFY(service.request(token, SongSearch{QStringLiteral("fixture"), 2, 0}) != 0);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(std::holds_alternative<SongSearchPage>(resultAt(done)));
    QCOMPARE(http->requests.first().url().path(), QStringLiteral("/eapi/search/get"));
    QVERIFY(service.request(token, DetailQuery{{{QStringLiteral("1")}}}) != 0);
    QTRY_COMPARE(done.size(), 2);
    QVERIFY(std::holds_alternative<SongDetails>(resultAt(done, 1)));
    QCOMPARE(http->requests.last().url().path(), QStringLiteral("/weapi/v3/song/detail"));
    QVERIFY(http->requests.last().rawHeader("Cookie").contains("MUSIC_A=synthetic-anon"));
    QVERIFY(service.request(token, LyricQuery{{QStringLiteral("1")}}) != 0);
    QTRY_COMPARE(done.size(), 3);
    QCOMPARE(std::get<SongLyrics>(resultAt(done, 2)).original.enhanced.timing, LyricTiming::Words);
    QVERIFY(service.request(token, SongSearch{QStringLiteral("limited")}) != 0);
    QTRY_COMPARE(done.size(), 4);
    const auto limited = std::get<MusicFailure>(resultAt(done, 3));
    QCOMPARE(limited.error, MusicError::RateLimited);
    QCOMPARE(limited.retryAfterMs, std::optional<qint64>(3000));
}

void MusicTest::cancellationAndSessionChange()
{
    SessionCoordinator session;
    auto manager = std::make_unique<FakeManager>();
    auto* http = manager.get();
    http->plans = {Plan{"{}", 200, -1}, Plan{"{}", 200, -1}};
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    QVERIFY(client.advanceTo(session.snapshot().token));
    connect(&session, &SessionCoordinator::changed, &client,
            [&](SessionSnapshot s) { static_cast<void>(client.advanceTo(s.token)); });
    MusicGateway gateway(client);
    MusicQueryService service(session, gateway);
    QSignalSpy done(&service, &MusicQueryService::completed);
    const auto token = session.snapshot().token;
    const auto id = service.request(token, SongSearch{QStringLiteral("fixture")});
    QTRY_COMPARE(http->requests.size(), 1);
    service.cancel(id);
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(std::get<MusicFailure>(resultAt(done)).error, MusicError::Canceled);
    QVERIFY(service.request(token, LyricQuery{{QStringLiteral("1")}}) != 0);
    QTRY_COMPARE(http->requests.size(), 2);
    session.signOut();
    QTRY_COMPARE(done.size(), 2);
    QCOMPARE(std::get<MusicFailure>(resultAt(done, 1)).error, MusicError::StaleContext);
    QCOMPARE(qvariant_cast<SessionToken>(done.last().at(1)), token);
}

void MusicTest::rejectsDuringAuthentication()
{
    SessionCoordinator session;
    auto manager = std::make_unique<FakeManager>();
    auto* http = manager.get();
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    QVERIFY(client.advanceTo(session.snapshot().token));
    MusicGateway gateway(client);
    MusicQueryService service(session, gateway);
    QSignalSpy done(&service, &MusicQueryService::completed);
    const auto attempt = session.beginAuthentication();
    QVERIFY(service.request(attempt, SongSearch{QStringLiteral("fixture")}) != 0);
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(std::get<MusicFailure>(resultAt(done)).error, MusicError::Busy);
    QVERIFY(http->requests.isEmpty());
}

QTEST_GUILESS_MAIN(MusicTest)
#include "music_test.moc"
