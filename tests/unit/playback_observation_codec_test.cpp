#include "../support/synthetic_playback_observation.h"
#include "application/playback/playback_observation_publisher.h"
#include "infrastructure/ipc/playback_observation_codec.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace cirrus;
using namespace domain;
using namespace infrastructure::ipc;
using Codec = PlaybackObservationCodec;
namespace
{
QByteArray request(QString method = "getSnapshot")
{
    return QJsonDocument(
               QJsonObject{{"v", 1}, {"kind", "request"}, {"id", "r"}, {"method", method}})
        .toJson(QJsonDocument::Compact);
}
ObservationRequest sliceRequest()
{
    ObservationRequest r;
    r.id = "page_1";
    r.method = ObservationMethod::LyricsSlice;
    r.binding = test::observationBinding();
    r.lyricsRevision = 2;
    r.maxLines = 128;
    return r;
}
QJsonObject sliceObject()
{
    auto o = QJsonDocument::fromJson(request("getLyricsSlice")).object();
    const auto r = sliceRequest();
    o["instanceId"] = r.binding.instanceId.toString(QUuid::WithoutBraces);
    o["viewEpoch"] = r.binding.viewEpoch.toString(QUuid::WithoutBraces);
    o["trackId"] = r.binding.trackId.toString(QUuid::WithoutBraces);
    o["lyricsRevision"] = "18446744073709551615";
    o["track"] = "original.standard";
    o["fromLine"] = 0;
    o["maxLines"] = 128;
    return o;
}
QJsonObject wire(const PlaybackObservation& p, const DocumentEncoding& e = {})
{
    const auto out = Codec::snapshot(p, e);
    return std::holds_alternative<QJsonObject>(out) ? std::get<QJsonObject>(out) : QJsonObject{};
}
} // namespace
class PlaybackObservationCodecTest final : public QObject
{
    Q_OBJECT
  private slots:
    void golden_data()
    {
        QTest::addColumn<bool>("unavailable");
        QTest::newRow("complete-ready-six-tracks") << false;
        QTest::newRow("unavailable-retracts-every-content-field") << true;
    }
    void golden()
    {
        QFETCH(bool, unavailable);
        auto p = test::observationValue();
        if (unavailable)
            p.status = ObservationStatus::Unavailable;
        QFile f(QString(OBSERVATION_FIXTURES) + (unavailable
                                                     ? "/playback_observation_unavailable_v1.json"
                                                     : "/playback_observation_snapshot_v1.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto expected = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(!expected.isEmpty());
        const auto e = Codec::prepare(test::observationDocument());
        QCOMPARE(e.status, ObservationContentStatus::Ready);
        QCOMPARE(wire(p, e), expected); // all keys/types/nulls; no per-field subset golden
        const auto body = QJsonDocument(wire(p, e)).toJson(QJsonDocument::Compact);
        QVERIFY(!body.contains("credit"));
        QVERIFY(!body.contains("SessionToken") && !body.contains("LoadToken") &&
                !body.contains("providerCode"));
        QCOMPARE(wire(p, e)["sequence"].toString(), "18446744073709551615");
    }
    void allMethodsAndWideCounter()
    {
        for (const auto& method : {"hello", "getSnapshot", "unsubscribe"})
            QVERIFY(std::holds_alternative<ObservationRequest>(Codec::decode(request(method))));
        auto o = QJsonDocument::fromJson(request("subscribe")).object();
        o["topics"] = QJsonArray{"snapshot", "lyrics", "cursor"};
        const auto r = Codec::decode(QJsonDocument(o).toJson(QJsonDocument::Compact));
        QVERIFY(std::holds_alternative<ObservationRequest>(r));
        QCOMPARE(std::get<ObservationRequest>(r).topics,
                 (QStringList{"snapshot", "lyrics", "cursor"}));
        const auto page =
            Codec::decode(QJsonDocument(sliceObject()).toJson(QJsonDocument::Compact));
        QVERIFY(std::holds_alternative<ObservationRequest>(page));
        QCOMPARE(std::get<ObservationRequest>(page).lyricsRevision,
                 std::numeric_limits<quint64>::max());
    }
    void malformed_data()
    {
        QTest::addColumn<QByteArray>("body");
        const QList<QByteArray> bad{
            "",
            "[]",
            "null",
            "{",
            request() + "{}",
            QByteArray("\xef\xbb\xbf") + request(),
            QByteArray("\xc0\xaf"),
            QByteArray("\xed\xa0\x80"),
            QByteArray("\xf4\x90\x80\x80"),
            "{\"v\":1,\"v\":1}",
            "{\"v\":1,\"\\u0076\":1}",
            "{\"x\":{\"a\":0,\"a\":1}}",
            "{\"id\":\"\\ud800\"}",
            "{\"id\":\"\\udc00\"}",
            "{\"id\":\"\\ud800x\"}",
            "{\"x\":01}",
            "{\"x\":+1}",
            "{\"x\":NaN}",
            "{\"x\":1e400}",
            "{\"x\":1.}",
            "{\"x\":1e}",
            "{\"x\":/*c*/0}",
            "{\"x\":0,}",
            "{\"x\":\"\\q\"}",
            "{\"v\":true,\"kind\":\"request\",\"id\":\"r\",\"method\":\"hello\"}",
            request("play"),
            request("openUri"),
            request("queryLyrics"),
            request("hello").replace("\"r\"", "null"),
            request("hello").replace("\"r\"", "\"雪\""),
            request("hello").replace("\"r\"", "\"bad.id\""),
            "{\"v\":1,\"kind\":\"request\",\"id\":\"r\",\"method\":\"hello\",\"extra\":0}"};
        int n = 0;
        for (const auto& b : bad)
            QTest::newRow(qPrintable(QString::number(n++))) << b;
        auto o = sliceObject();
        for (const auto& key : o.keys()) {
            auto x = o;
            x.remove(key);
            QTest::newRow(qPrintable("missing-" + key))
                << QJsonDocument(x).toJson(QJsonDocument::Compact);
        }
        for (const auto& value : {QJsonValue(9007199254740992.0), QJsonValue("01"),
                                  QJsonValue("+1"), QJsonValue("18446744073709551616"),
                                  QJsonValue(""), QJsonValue(" 1"), QJsonValue("١")}) {
            auto x = o;
            x["lyricsRevision"] = value;
            QTest::newRow(qPrintable("counter-" + QString::number(n++)))
                << QJsonDocument(x).toJson(QJsonDocument::Compact);
        }
        for (const auto& value : {QJsonValue(-1), QJsonValue(10001), QJsonValue(1.25),
                                  QJsonValue("0"), QJsonValue(true)}) {
            auto x = o;
            x["fromLine"] = value;
            QTest::newRow(qPrintable("from-" + QString::number(n++)))
                << QJsonDocument(x).toJson(QJsonDocument::Compact);
        }
        for (const auto& value :
             {QJsonValue(0), QJsonValue(129), QJsonValue(1.5), QJsonValue("1")}) {
            auto x = o;
            x["maxLines"] = value;
            QTest::newRow(qPrintable("max-" + QString::number(n++)))
                << QJsonDocument(x).toJson(QJsonDocument::Compact);
        }
        for (const auto& value :
             {QJsonValue("original.yrc"), QJsonValue("ORIGINAL.STANDARD"), QJsonValue(0)}) {
            auto x = o;
            x["track"] = value;
            QTest::newRow(qPrintable("track-" + QString::number(n++)))
                << QJsonDocument(x).toJson(QJsonDocument::Compact);
        }
        for (const auto& value :
             {QJsonValue("00000000-0000-0000-0000-000000000000"),
              QJsonValue("{00000000-0000-4000-8000-000000000001}"), QJsonValue(1)}) {
            auto x = o;
            x["instanceId"] = value;
            QTest::newRow(qPrintable("uuid-" + QString::number(n++)))
                << QJsonDocument(x).toJson(QJsonDocument::Compact);
        }
        for (const auto& value :
             {QJsonValue(QJsonArray{}), QJsonValue(QJsonArray{"cursor", "cursor"}),
              QJsonValue(QJsonArray{"play"}), QJsonValue("cursor")}) {
            auto x = QJsonDocument::fromJson(request("subscribe")).object();
            x["topics"] = value;
            QTest::newRow(qPrintable("topics-" + QString::number(n++)))
                << QJsonDocument(x).toJson(QJsonDocument::Compact);
        }
    }
    void malformed()
    {
        QFETCH(QByteArray, body);
        const auto r = Codec::decode(body);
        QVERIFY(std::holds_alternative<RequestFailure>(r));
        QCOMPARE(std::get<RequestFailure>(r).code, ObservationError::InvalidRequest);
    }
    void duplicateAndDepthAreNotQtFallback()
    {
        for (auto key : {QByteArray("id"), QByteArray("\\u0069d")}) {
            const auto b =
                QByteArray("{\"v\":1,\"kind\":\"request\",\"id\":\"r\",\"method\":\"hello\",\"") +
                key + "\":\"r\"}";
            const auto r = Codec::decode(b);
            QVERIFY(std::holds_alternative<RequestFailure>(r));
            QVERIFY(std::get<RequestFailure>(r)
                        .id.isEmpty()); // duplicate pass failed BEFORE QJson read
        }
        for (int depth : {8, 9}) {
            auto b = request("hello");
            b.chop(1);
            b.append(",\"extra\":");
            b.append(QByteArray(depth - 1, '['));
            b.append('0');
            b.append(QByteArray(depth - 1, ']'));
            b.append('}');
            const auto r = std::get<RequestFailure>(Codec::decode(b));
            QCOMPARE(r.id.isEmpty(), depth == 9); // depth8 passes grammar, fails unknown field
        }
        const auto unicode = Codec::decode("{\"v\":1,\"kind\":\"request\",\"id\":\"r\",\"method\":"
                                           "\"hello\",\"extra\":\"\\ud83d\\ude00\"}");
        QCOMPARE(std::get<RequestFailure>(unicode).id,
                 "r"); // valid surrogate pair, semantic unknown field
    }
    void sizeAndVersionBoundaries()
    {
        auto b = request("hello");
        b.append(QByteArray(Codec::RequestBytes - b.size(), ' '));
        QVERIFY(std::holds_alternative<ObservationRequest>(Codec::decode(b)));
        b.append(' ');
        QVERIFY(std::holds_alternative<RequestFailure>(Codec::decode(b)));
        auto o = QJsonDocument::fromJson(request("hello")).object();
        o["v"] = 2;
        const auto fail = std::get<RequestFailure>(
            Codec::decode(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QCOMPARE(fail.code, ObservationError::UnsupportedVersion);
        QCOMPARE(fail.id, "r");
        o["v"] = 1;
        o["id"] = QString(64, u'A');
        QVERIFY(std::holds_alternative<ObservationRequest>(
            Codec::decode(QJsonDocument(o).toJson(QJsonDocument::Compact))));
        o["id"] = QString(65, u'A');
        QVERIFY(std::holds_alternative<RequestFailure>(
            Codec::decode(QJsonDocument(o).toJson(QJsonDocument::Compact))));
    }
    void fractionalLexemesNeverRoundToInteger()
    {
        auto b = QJsonDocument(sliceObject()).toJson(QJsonDocument::Compact);
        for (const auto replacement :
             {QByteArray("128.00000000000000001"), QByteArray("1.28e2"), QByteArray("128.0")}) {
            auto x = b;
            x.replace("\"maxLines\":128", QByteArray("\"maxLines\":") + replacement);
            QVERIFY(std::holds_alternative<RequestFailure>(Codec::decode(x)));
        }
    }
    void normalizedBytesAndExactAggregateBoundary()
    {
        auto d = test::observationDocument();
        auto e = Codec::prepare(d);
        auto p = test::observationValue();
        QJsonObject normalizedTracks;
        const auto ws = wire(p, e)["lyrics"].toObject()["tracks"].toObject();
        for (int i = 0; i < 6; ++i) {
            auto r = sliceRequest();
            r.track = i;
            const auto page =
                QJsonDocument::fromJson(std::get<QByteArray>(Codec::slice(r, p, e))).object();
            auto s = ws[Codec::trackNames()[size_t(i)]].toObject()["summary"].toObject();
            s["lines"] = page["lines"];
            normalizedTracks[Codec::trackNames()[size_t(i)]] = s;
        }
        QCOMPARE(e.document->normalizedBytes(),
                 QJsonDocument(QJsonObject{{"tracks", normalizedTracks}})
                     .toJson(QJsonDocument::Compact)
                     .size());
        // Structural overhead dominates; string budget stays below 1Mi UTF16.
        std::array<LyricTrack*, 4> ts{&d.lyrics.original.standard, &d.lyrics.original.enhanced,
                                      &d.lyrics.translation.standard,
                                      &d.lyrics.translation.enhanced};
        for (auto* t : ts) {
            t->availability = LyricAvailability::Present;
            t->timing = LyricTiming::Words;
            t->lines = {{0, 5000, "", QList<LyricWord>(30000, LyricWord{0, 5000, "x"})}};
        }
        const auto base = Codec::prepare(d);
        QCOMPARE(base.status, ObservationContentStatus::Ready);
        const auto gap = Codec::DocumentBytes - base.document->normalizedBytes();
        QVERIFY(gap > 0);
        for (qsizetype delta : {-1, 0, 1}) {
            const auto n = gap + delta;
            d.lyrics.original.standard.lines[0].text =
                QString(n / 6, QChar(1)) + QString(n % 6, u'x');
            const auto encoded = Codec::prepare(d);
            QCOMPARE(encoded.status, delta > 0 ? ObservationContentStatus::LimitExceeded
                                               : ObservationContentStatus::Ready);
            if (delta <= 0)
                QCOMPARE(encoded.document->normalizedBytes(), Codec::DocumentBytes + delta);
        }
    }
    void snapshotInvalidBase_data()
    {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < 14; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void snapshotInvalidBase()
    {
        QFETCH(int, mode);
        auto p = test::observationValue();
        if (mode == 0)
            p.binding.instanceId = {};
        if (mode == 1)
            p.state = static_cast<PlaybackState>(99);
        if (mode == 2)
            p.sampleMonoMs = -1;
        if (mode == 3)
            p.positionObservedMonoMs = 1501;
        if (mode == 4)
            p.volumeLinear = std::numeric_limits<float>::quiet_NaN();
        if (mode == 5)
            p.durationMs = 0;
        if (mode == 6)
            p.positionMs = -1;
        if (mode == 7)
            p.songPositionMs = 42;
        if (mode == 8)
            p.preview = PreviewRange{0, 0};
        if (mode == 9)
            p.binding.trackId = {};
        if (mode == 10)
            p.failure = static_cast<PlaybackError>(99);
        if (mode == 11)
            p.userOffsetMs = 120001;
        if (mode == 12)
            p.mode = ListeningMode::PersonalFm; // don't fabricate FM queue settings
        if (mode == 13)
            p.durationMs = 30001; // preview clip has only30000 playable ms
        const auto r = Codec::snapshot(p, Codec::prepare(test::observationDocument()));
        QVERIFY(std::holds_alternative<ObservationError>(r));
        QCOMPARE(std::get<ObservationError>(r), ObservationError::InvalidRequest);
    }
    void invalidStatusesDoNotInventWireEnums()
    {
        auto p = test::observationValue();
        p.metadataStatus = p.cursorStatus = static_cast<ObservationContentStatus>(99);
        const auto w = wire(p, Codec::prepare(test::observationDocument()));
        QCOMPARE(w["metadataExportStatus"].toString(), QString("invalidData"));
        QCOMPARE(w["lyrics"].toObject()["cursorExportStatus"].toString(), QString("invalidData"));
        QVERIFY(w["metadata"].isNull());
    }
    void inconsistentSummariesCannotServeSlices()
    {
        const auto e = Codec::prepare(test::observationDocument());
        auto p = test::observationValue();
        ++(*p.lyricTracks)[0].lineCount;
        auto result = Codec::slice(sliceRequest(), p, e);
        QCOMPARE(std::get<ObservationError>(result), ObservationError::LyricsNotReady);
        p.lyricTracks.reset();
        result = Codec::slice(sliceRequest(), p, e);
        QCOMPARE(std::get<ObservationError>(result), ObservationError::LyricsNotReady);
    }
    void streamingCutsConcatenationAndDeadline()
    {
        const auto body = request();
        const auto framed = Codec::frame(body);
        for (qsizetype cut = 0; cut <= framed.size(); ++cut) {
            ObservationRequestFramer f;
            auto a = f.feed(QByteArrayView(framed).first(cut), 0);
            auto b = f.feed(QByteArrayView(framed).sliced(cut), 1);
            QVERIFY(!a.closed && !b.closed);
            QCOMPARE(a.body ? *a.body : *b.body, body);
            QCOMPARE(f.bufferedBytes(), 0);
        }
        ObservationRequestFramer bytewise;
        for (qsizetype i = 0; i < framed.size(); ++i) {
            const auto r = bytewise.feed(QByteArrayView(framed).sliced(i, 1), 0);
            QVERIFY(!r.closed);
            QCOMPARE(r.body.has_value(), i + 1 == framed.size());
        }
        const auto many = framed + framed + framed;
        ObservationRequestFramer combined;
        qsizetype used = 0;
        for (int n = 0; n < 3; ++n) {
            const auto r = combined.feed(QByteArrayView(many).sliced(used), 0);
            QVERIFY(r.body);
            used += r.consumed;
            QCOMPARE(*r.body, body);
        }
        QCOMPARE(used, many.size());
        ObservationRequestFramer slow;
        QVERIFY(!slow.feed(QByteArrayView(framed).first(1), 0).closed);
        QVERIFY(!slow.feed(QByteArrayView(framed).sliced(1, 1), 1999).closed);
        QVERIFY(slow.feed(QByteArrayView(framed).sliced(2), 2000).closed);
        QCOMPARE(slow.bufferedBytes(), 0);
        QVERIFY(slow.feed(framed, 2001).closed);
        ObservationRequestFramer regressed;
        regressed.feed(QByteArrayView(framed).first(1), 10);
        QVERIFY(regressed.feed({}, 9).closed);
        for (const auto prefix : {QByteArray(4, '\0'), QByteArray::fromHex("00001001"),
                                  QByteArray::fromHex("ffffffff")}) {
            ObservationRequestFramer f;
            const auto r = f.feed(prefix + QByteArray(100, 'x'), 0);
            QVERIFY(r.closed);
            QCOMPARE(r.consumed, 4);
            QCOMPARE(f.bufferedBytes(), 0);
        }
        QVERIFY(Codec::frame({}).isEmpty());
        QVERIFY(!Codec::frame(QByteArray(Codec::FrameBytes, 'x')).isEmpty());
        QVERIFY(Codec::frame(QByteArray(Codec::FrameBytes + 1, 'x')).isEmpty());
    }
    void slicePreservesIdentityAndText()
    {
        const auto p = test::observationValue();
        const auto doc = test::observationDocument();
        const auto e = Codec::prepare(doc);
        auto r = sliceRequest();
        r.maxLines = 1;
        auto value = Codec::slice(r, p, e);
        QVERIFY(std::holds_alternative<QByteArray>(value));
        auto page = QJsonDocument::fromJson(std::get<QByteArray>(value)).object();
        QCOMPARE(page["lines"].toArray()[0].toObject()["text"].toString(),
                 doc.lyrics.original.standard.lines[0].text);
        QCOMPARE(page["nextLine"].toInt(), 1);
        QVERIFY(!std::get<QByteArray>(value).contains("credit"));
        r.fromLine = 1;
        r.maxLines = 128;
        value = Codec::slice(r, p, e);
        page = QJsonDocument::fromJson(std::get<QByteArray>(value)).object();
        QCOMPARE(page["lines"].toArray().size(), 2);
        QVERIFY(page["nextLine"].isNull());
        QCOMPARE(page["lines"].toArray()[1].toObject()["endMs"], QJsonValue{});
        for (int t : {3, 4}) {
            r.track = t;
            r.fromLine = 0;
            page = QJsonDocument::fromJson(std::get<QByteArray>(Codec::slice(r, p, e))).object();
            QCOMPARE(page["lines"].toArray().size(), 0);
            QVERIFY(page["nextLine"].isNull());
        }
        r.track = 0;
        r.fromLine = 3;
        QVERIFY(std::holds_alternative<QByteArray>(Codec::slice(r, p, e)));
        r.fromLine = 4;
        QCOMPARE(std::get<ObservationError>(Codec::slice(r, p, e)),
                 ObservationError::InvalidRequest);
        r.fromLine = 0;
        for (int n = 0; n < 4; ++n) {
            auto old = r;
            if (n == 0)
                old.binding.instanceId = QUuid::createUuid();
            if (n == 1)
                old.binding.viewEpoch = QUuid::createUuid();
            if (n == 2)
                old.binding.trackId = QUuid::createUuid();
            if (n == 3)
                ++old.lyricsRevision;
            QCOMPARE(std::get<ObservationError>(Codec::slice(old, p, e)),
                     ObservationError::StaleView);
        }
        auto sought = p;
        sought.positionMs = 2000;
        sought.songPositionMs = 12000;
        ++sought.seekRevision;
        QVERIFY(std::holds_alternative<QByteArray>(Codec::slice(r, sought, e)));
        QCOMPARE(std::get<ObservationError>(Codec::slice(r, p, {})),
                 ObservationError::LyricsNotReady);
        r.id = QString(100000, u'x');
        QCOMPARE(std::get<ObservationError>(Codec::slice(r, p, e)),
                 ObservationError::InvalidRequest);
    }
    void prepareBudgetsAndInvalidValues_data()
    {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < 10; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void prepareBudgetsAndInvalidValues()
    {
        QFETCH(int, mode);
        auto d = test::observationDocument();
        auto& t = d.lyrics.original.standard;
        if (mode == 0)
            t.lines[0].text = QString(Codec::TextUnits + 1, u'a');
        if (mode == 1)
            t.lines = QList<LyricLine>(10001);
        if (mode == 2)
            t.lines[0].words = QList<LyricWord>(30001);
        if (mode == 3)
            t.lines[0].text = QString(QChar(0xd800));
        if (mode == 4)
            t.lines[0].text = QString(QChar::Null);
        if (mode == 5)
            t.lines[0].startMs = -1;
        if (mode == 6)
            t.lines[0].words = {{ObservedPlayback::MaximumMs, 1, "x"}};
        if (mode == 7)
            t.availability = static_cast<LyricAvailability>(99);
        if (mode == 8)
            t.providerOffsetMs = ObservedPlayback::MaximumMs + 1;
        if (mode == 9)
            d.binding.trackId = {};
        const auto e = Codec::prepare(d);
        QVERIFY(!e.document);
        QCOMPARE(e.status, mode < 3 ? ObservationContentStatus::LimitExceeded
                                    : ObservationContentStatus::InvalidData);
    }
    void documentWireAndSingleLineBudgets()
    {
        auto d = test::observationDocument();
        // Maximum source line count fits when text is small; no slicing truncation.
        d.lyrics.original.standard.lines = QList<LyricLine>(10000, LyricLine{0, 5000, "x", {}});
        auto e = Codec::prepare(d);
        QCOMPARE(e.status, ObservationContentStatus::Ready);
        auto p = test::observationValue();
        test::observationSummaries(p, d.lyrics);
        auto r = sliceRequest();
        const auto page =
            QJsonDocument::fromJson(std::get<QByteArray>(Codec::slice(r, p, e))).object();
        QCOMPARE(page["lines"].toArray().size(), 128);
        // One fully retained line >1MiB: document valid, slice explicitly refuses it.
        d = test::observationDocument();
        d.lyrics.original.enhanced.lines[0].words =
            QList<LyricWord>(24000, LyricWord{0, 5000, "x"});
        e = Codec::prepare(d);
        QCOMPARE(e.status, ObservationContentStatus::Ready);
        p = test::observationValue();
        test::observationSummaries(p, d.lyrics);
        r.track = 1;
        QCOMPARE(std::get<ObservationError>(Codec::slice(r, p, e)),
                 ObservationError::ExportLimitExceeded);
        // 6*30000 words: text still <1Mi UTF16, normalized JSON exceeds 8MiB.
        for (auto* t :
             std::array{&d.lyrics.original.standard, &d.lyrics.original.enhanced,
                        &d.lyrics.translation.standard, &d.lyrics.translation.enhanced,
                        &d.lyrics.romanization.standard, &d.lyrics.romanization.enhanced}) {
            t->availability = LyricAvailability::Present;
            t->timing = LyricTiming::Words;
            t->lines = {{0, 5000, "x", QList<LyricWord>(30000, LyricWord{0, 5000, "x"})}};
        }
        e = Codec::prepare(d);
        QCOMPARE(e.status, ObservationContentStatus::LimitExceeded);
        QVERIFY(!e.document);
    }
    void snapshotDegradesWithoutChangingOwnerValues()
    {
        auto d = test::observationDocument();
        auto p = test::observationValue();
        d.lyrics.original.enhanced.lines[0].words =
            QList<LyricWord>(30000, LyricWord{0, 5000, "x"});
        test::observationSummaries(p, d.lyrics);
        p.cursors->original.enhanced.words.clear();
        for (int i = 0; i < 30000; ++i)
            p.cursors->original.enhanced.words.append({0, i});
        const auto e = Codec::prepare(d);
        auto s = wire(p, e);
        QVERIFY(!s.isEmpty());
        QCOMPARE(s["lyrics"].toObject()["cursorExportStatus"].toString(), "limitExceeded");
        const auto ts = s["lyrics"].toObject()["tracks"].toObject();
        for (const auto& k : Codec::trackNames())
            QVERIFY(ts[k].toObject()["cursor"].isNull());
        QCOMPARE(p.cursors->original.enhanced.words.size(), 30000);
        QVERIFY(QJsonDocument(s).toJson(QJsonDocument::Compact).size() <= Codec::SnapshotBytes);
        p = test::observationValue();
        p.metadata->title = QString(4097, u'x');
        s = wire(p, Codec::prepare(test::observationDocument()));
        QVERIFY(s["metadata"].isNull());
        QCOMPARE(s["metadataExportStatus"].toString(), "limitExceeded");
        p = test::observationValue();
        p.cursors->original.enhanced.words = {{0, 2}};
        s = wire(p, Codec::prepare(test::observationDocument()));
        QCOMPARE(s["lyrics"].toObject()["cursorExportStatus"].toString(), "invalidData");
        p = test::observationValue();
        s = wire(p);
        QCOMPARE(s["lyrics"].toObject()["documentExportStatus"].toString(), "preparing");
        (*p.lyricTracks)[0].lineCount = 2;
        s = wire(p, Codec::prepare(test::observationDocument()));
        QCOMPARE(s["lyrics"].toObject()["documentExportStatus"].toString(), "invalidData");
    }
    void fixedFailureCodes_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("name");
        const QStringList names{"InvalidRequest",
                                "StaleContext",
                                "Canceled",
                                "Unavailable",
                                "ProtocolKeyMissing",
                                "Network",
                                "Timeout",
                                "RateLimited",
                                "InvalidResponse",
                                "UnsafeSource",
                                "Expired",
                                "TooLarge",
                                "Storage",
                                "UnsupportedFormat",
                                "Decode",
                                "NoOutput"};
        for (int i = 0; i < names.size(); ++i)
            QTest::newRow(qPrintable(names[i])) << i << names[i];
    }
    void fixedFailureCodes()
    {
        QFETCH(int, code);
        QFETCH(QString, name);
        auto p = test::observationValue();
        p.failure = static_cast<PlaybackError>(code);
        QCOMPARE(wire(p, Codec::prepare(test::observationDocument()))["failure"].toObject(),
                 (QJsonObject{{"code", name}}));
    }
    void fakeChunkConsumerHasNoBusinessPorts()
    {
        application::BackendFacade f;
        application::ListeningSession o(f);
        application::PlaybackObservationPublisher pub(o, f);
        f.start();
        application::BackendEvent e;
        e.kind = application::BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = application::BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
        f.deliver(e);
        QTRY_COMPARE(pub.snapshot().status, ObservationStatus::Ready);
        QSignalSpy playback(&f, &application::BackendFacade::playbackRequested),
            music(&f, &application::BackendFacade::musicRequested);
        ObservationRequestFramer framer;
        QByteArray packets = Codec::frame(request("hello")) + Codec::frame(request()) +
                             Codec::frame(request("play"));
        int accepted = 0, failed = 0;
        for (qsizetype offset = 0; offset < packets.size();) {
            const auto r = framer.feed(QByteArrayView(packets).sliced(offset), 0);
            QVERIFY(!r.closed);
            QVERIFY(r.body);
            offset += r.consumed;
            const auto decoded = Codec::decode(*r.body);
            if (std::holds_alternative<ObservationRequest>(decoded)) {
                ++accepted;
                if (std::get<ObservationRequest>(decoded).method == ObservationMethod::Snapshot)
                    QVERIFY(!wire(pub.snapshot()).isEmpty());
            } else
                ++failed;
        }
        QCOMPARE(accepted, 2);
        QCOMPARE(failed, 1);
        QCOMPARE(playback.size(), 0);
        QCOMPARE(music.size(), 0);
    }
};
QTEST_GUILESS_MAIN(PlaybackObservationCodecTest)
#include "playback_observation_codec_test.moc"
