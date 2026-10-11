#include "../support/synthetic_playback_observation.h"
#include "infrastructure/ipc/playback_observation_codec.h"
#include "infrastructure/ipc/playback_observation_connection.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <functional>
#include <thread>

using namespace cirrus;
using namespace cirrus::infrastructure::ipc;
using namespace cirrus::domain;
using Codec = PlaybackObservationCodec;
namespace
{
class Sink final : public IObservationWriteSink
{
  public:
    qint64 pendingBytes() const override { return queued.size(); }
    qint64 write(QByteArrayView view) override
    {
        ++writes;
        if (fail)
            return -1;
        const auto n = std::min<qsizetype>(limit, view.size());
        if (immediate)
            delivered.append(view.data(), n);
        else
            queued.append(view.data(), n);
        if (onWrite) {
            auto action = std::move(onWrite);
            onWrite = {};
            action();
        }
        return n;
    }
    void closeAndDiscard() override
    {
        ++closes;
        queued.clear();
    }
    void drain(qsizetype n)
    {
        n = std::min(n, queued.size());
        delivered.append(queued.data(), n);
        queued.remove(0, n);
    }
    QList<QJsonObject> messages()
    {
        QList<QJsonObject> out;
        qsizetype at = 0;
        while (delivered.size() - at >= 4) {
            quint32 n = 0;
            for (int i = 0; i < 4; ++i)
                n = (n << 8) | quint8(delivered[at + i]);
            if (n > quint32(Codec::FrameBytes) || delivered.size() - at - 4 < n)
                break;
            out.append(QJsonDocument::fromJson(delivered.mid(at + 4, n)).object());
            at += 4 + n;
        }
        delivered.remove(0, at);
        return out;
    }
    QByteArray queued, delivered;
    std::function<void()> onWrite;
    qsizetype limit = 65536;
    int writes = 0, closes = 0;
    bool immediate = true, fail = false;
};
QByteArray request(const QString& method, const QString& id, QJsonObject extra = {})
{
    QJsonObject o{{"v", 1}, {"kind", "request"}, {"id", id}, {"method", method}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        o[it.key()] = it.value();
    return Codec::frame(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QJsonObject sliceExtra(const PlaybackObservation& p)
{
    return {{"instanceId", p.binding.instanceId.toString(QUuid::WithoutBraces)},
            {"viewEpoch", p.binding.viewEpoch.toString(QUuid::WithoutBraces)},
            {"trackId", p.binding.trackId.toString(QUuid::WithoutBraces)},
            {"lyricsRevision", QString::number(p.lyricsRevision)},
            {"track", "original.standard"},
            {"fromLine", 0},
            {"maxLines", 128}};
}
struct Rig {
    std::array<Sink, 9> sinks; // sinks outlive the engine
    std::shared_ptr<ObservationMailbox> box = std::make_shared<ObservationMailbox>();
    PlaybackObservation p = test::observationValue();
    std::shared_ptr<const PlaybackLyricDocument> document =
        std::make_shared<const PlaybackLyricDocument>(test::observationDocument());
    PlaybackObservationConnections engine{box};
    quint64 client = 0;
    qint64 now = 0;
    Rig()
    {
        p.sequence = 1;
        box->publish(p, document);
        client = engine.open(sinks[0], now);
    }
    void publish()
    {
        ++p.sequence;
        box->publish(p, document);
    }
    void send(const QString& method, const QString& id, QJsonObject extra = {})
    {
        engine.receive(client, request(method, id, extra), now);
        engine.pump(now);
    }
    QList<QJsonObject> flush()
    {
        for (int i = 0; i < 100; ++i)
            engine.write(client, now);
        return sinks[0].messages();
    }
    void hello()
    {
        send("hello", "hello");
        flush();
        while (engine.encodingPending())
            engine.pump(now);
        flush();
    }
};
} // namespace
class PlaybackObservationConnectionTest final : public QObject
{
    Q_OBJECT
  private slots:
    void chunkedHelloAndPipeline()
    {
        const auto hello = request("hello", "h"), snapshot = request("getSnapshot", "s");
        for (qsizetype cut = 0; cut <= hello.size(); ++cut) {
            Rig r;
            QCOMPARE(r.engine.receive(r.client, QByteArrayView(hello).first(cut), 0), cut);
            const auto rest = hello.mid(cut) + snapshot;
            QCOMPARE(r.engine.receive(r.client, rest, 1), rest.size());
            r.now = 1;
            r.engine.pump(1);
            r.engine.pump(1);
            const auto m = r.flush();
            QCOMPARE(m.size(), 2);
            QCOMPARE(m[0]["id"].toString(), QString("h"));
            QCOMPARE(m[1]["id"].toString(), QString("s"));
        }
    }
    void malformedWithoutCorrelationCloses()
    {
        Rig r;
        r.hello();
        r.engine.receive(
            r.client,
            Codec::frame("{\"v\":1,\"kind\":\"request\",\"id\":null,\"method\":\"play\"}"), 0);
        QVERIFY(!r.engine.isOpen(r.client));
        QVERIFY(r.sinks[0].delivered.isEmpty());
    }
    void eightSlowClientsStayWithinAggregateCap()
    {
        Rig r;
        std::array<quint64, 8> ids{};
        ids[0] = r.client;
        for (int i = 1; i < 8; ++i)
            ids[size_t(i)] = r.engine.open(r.sinks[size_t(i)], 0);
        for (int i = 0; i < 8; ++i) {
            auto& s = r.sinks[size_t(i)];
            s.immediate = false;
            s.queued.resize(PlaybackObservationConnections::ClientBytes - 8192);
            r.engine.receive(ids[size_t(i)], request("hello", QString::number(i)), 0);
        }
        r.engine.pump(0);
        qint64 sum = 0;
        for (int i = 0; i < 8; ++i) {
            r.engine.write(ids[size_t(i)], 0);
            sum += r.sinks[size_t(i)].pendingBytes() + r.engine.applicationBytes(ids[size_t(i)]);
        }
        QVERIFY(sum <= 8 * PlaybackObservationConnections::ClientBytes);
        for (int i = 0; i < 1000; ++i) {
            ++r.p.userOffsetMs;
            r.publish();
        }
        QVERIFY(r.box->wakePending());
        r.engine.pump(5000);
        QCOMPARE(r.engine.clientCount(), 0);
        QVERIFY(!r.box->wakePending());
        QCOMPARE(r.p.state, PlaybackState::Paused);
    }
    void helloFirstAndDeadlines_data()
    {
        QTest::addColumn<int>("mode");
        for (int n = 0; n < 5; ++n)
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void helloFirstAndDeadlines()
    {
        QFETCH(int, mode);
        Rig r;
        QVERIFY(r.client);
        if (mode == 0)
            r.engine.receive(r.client, request("getSnapshot", "x"), 0);
        if (mode == 1)
            r.engine.pump(5000);
        if (mode == 2) {
            const auto f = request("hello", "x");
            r.engine.receive(r.client, QByteArrayView(f).first(1), 0);
            r.engine.pump(2000);
        }
        if (mode == 3) {
            r.engine.receive(r.client, request("hello", "x"), 4999);
            r.engine.pump(4999);
            QVERIFY(r.engine.isOpen(r.client));
            r.engine.pump(6000);
            QVERIFY(r.engine.isOpen(r.client));
            return;
        }
        if (mode == 4) {
            r.engine.receive(r.client, request("hello", "x"), 0);
            r.engine.pump(5000);
        }
        QVERIFY(!r.engine.isOpen(r.client));
        QCOMPARE(r.sinks[0].closes, 1);
    }
    void helloAndReadOnlyMethods()
    {
        Rig r;
        r.send("hello", "h");
        auto m = r.flush();
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0]["kind"].toString(), QString("response"));
        QCOMPARE(m[0]["methods"].toArray().size(), 5);
        QCOMPARE(m[0]["tracks"].toArray().size(), 6);
        QCOMPARE(m[0]["limits"].toObject()["inputBytes"].toInt(), 16384);
        while (r.engine.encodingPending())
            r.engine.pump(0);
        r.send("getSnapshot", "s");
        m = r.flush();
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0]["snapshot"].toObject()["sequence"].toString(), QString("1"));
        r.send("getLyricsSlice", "l", sliceExtra(r.p));
        m = r.flush();
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0]["lines"].toArray().size(), 3);
        r.send("hello", "again");
        m = r.flush();
        QCOMPARE(m[0]["error"].toObject()["code"].toString(), QString("InvalidRequest"));
    }
    void forbiddenMethods_data()
    {
        QTest::addColumn<QString>("method");
        for (const auto& s : QStringList{"play", "seek", "setVolume", "openUri", "login",
                                         "queryLyrics", "loadSong", "getHistory"})
            QTest::newRow(qPrintable(s)) << s;
    }
    void forbiddenMethods()
    {
        QFETCH(QString, method);
        Rig r;
        r.hello();
        r.send(method, "x");
        const auto m = r.flush();
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0]["error"].toObject()["code"].toString(), QString("InvalidRequest"));
        // This target has no application, Network, SQL or media linkage/command seam.
        QCOMPARE(r.p.sequence, quint64(1));
    }
    void pendingAndDuplicateCorrelation()
    {
        Rig r;
        r.hello();
        QByteArray batch;
        for (int i = 0; i < 5; ++i)
            batch += request("getSnapshot", QString::number(i));
        const auto used = r.engine.receive(r.client, batch, 0);
        QVERIFY(used < batch.size());
        QCOMPARE(r.engine.pendingRequests(r.client), 4);
        QCOMPARE(r.engine.receive(r.client, QByteArrayView(batch).sliced(used), 0), qsizetype(0));
        for (int i = 0; i < 4; ++i)
            r.engine.pump(0);
        // ID remains pending until the WHOLE response is admitted, not just computed.
        r.sinks[0].limit = 1;
        r.engine.write(r.client, 0);
        QCOMPARE(r.engine.pendingRequests(r.client), 4);
        r.sinks[0].limit = 65536;
        auto m = r.flush();
        QCOMPARE(m.size(), 4);
        QCOMPARE(r.engine.pendingRequests(r.client), 0);
        QCOMPARE(r.engine.receive(r.client, QByteArrayView(batch).sliced(used), 0),
                 batch.size() - used);
        r.engine.pump(0);
        m = r.flush();
        QCOMPARE(m.size(), 1);
        const auto duplicate = request("getSnapshot", "dup");
        r.engine.receive(r.client, duplicate + duplicate, 0);
        QVERIFY(!r.engine.isOpen(r.client));
        QCOMPARE(r.engine.lastClose(), ObservationClose::Protocol);
    }
    void rateBurstAndRefill()
    {
        Rig r;
        r.hello();
        for (int i = 0; i < 15; ++i) {
            r.send("getSnapshot", QString::number(i));
            r.flush();
            QVERIFY(r.engine.isOpen(r.client));
        }
        r.send("getSnapshot", "17");
        QVERIFY(!r.engine.isOpen(r.client));
        QCOMPARE(r.engine.lastClose(), ObservationClose::RateLimit);
        Rig good;
        good.hello();
        for (int i = 0; i < 48; ++i) {
            good.now = 1000 + i * 63;
            good.send("getSnapshot", QString::number(i));
            good.flush();
            QVERIFY(good.engine.isOpen(good.client));
        }
    }
    void topicPriority_data()
    {
        QTest::addColumn<QStringList>("names");
        QTest::addColumn<QString>("expected");
        QTest::newRow("snapshot") << QStringList{"snapshot"} << QString("snapshot");
        QTest::newRow("lyrics") << QStringList{"lyrics"} << QString("lyrics");
        QTest::newRow("cursor") << QStringList{"cursor"} << QString("cursor");
        QTest::newRow("snapshot-lyrics")
            << QStringList{"snapshot", "lyrics"} << QString("snapshot");
        QTest::newRow("snapshot-cursor")
            << QStringList{"snapshot", "cursor"} << QString("snapshot");
        QTest::newRow("lyrics-cursor") << QStringList{"lyrics", "cursor"} << QString("lyrics");
        QTest::newRow("all") << QStringList{"snapshot", "lyrics", "cursor"} << QString("snapshot");
    }
    void topicPriority()
    {
        QFETCH(QStringList, names);
        QFETCH(QString, expected);
        Rig r;
        r.hello();
        QJsonArray topics;
        for (const auto& n : names)
            topics.append(n);
        r.send("subscribe", "sub", {{"topics", topics}});
        // A later update must follow baseline even before first output byte.
        ++r.p.userOffsetMs;
        r.publish();
        r.engine.pump(0);
        const auto m = r.flush();
        QCOMPARE(m.size(), 2);
        QCOMPARE(m[0]["id"].toString(), QString("sub"));
        QCOMPARE(m[0]["snapshot"].toObject()["sequence"].toString(), QString("1"));
        QCOMPARE(m[1]["topic"].toString(), expected);
        QCOMPARE(m[1]["snapshot"].toObject()["sequence"].toString(), QString("2"));
    }
    void positionOnlyAndLatestNoReplay()
    {
        Rig r;
        r.hello();
        r.send("subscribe", "sub", {{"topics", QJsonArray{"lyrics", "cursor"}}});
        r.flush();
        ++*r.p.positionMs;
        ++*r.p.songPositionMs;
        r.publish();
        r.engine.pump(0);
        QVERIFY(r.flush().isEmpty());
        r.send("subscribe", "replace", {{"topics", QJsonArray{"snapshot"}}});
        r.flush();
        for (int i = 0; i < 10000; ++i) {
            ++*r.p.positionMs;
            ++*r.p.songPositionMs;
            r.p.durationMs = 40000;
            r.p.preview.reset();
            r.p.songPositionMs = r.p.positionMs;
            r.publish();
        }
        QVERIFY(r.box->wakePending());
        r.engine.pump(0);
        QVERIFY(!r.box->wakePending());
        const auto m = r.flush();
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0]["snapshot"].toObject()["sequence"].toString(), QString::number(r.p.sequence));
        r.send("unsubscribe", "u");
        r.flush();
        ++r.p.userOffsetMs;
        r.publish();
        r.engine.pump(0);
        QVERIFY(r.flush().isEmpty());
    }
    void unsubscribeDoesNotCutPartialFrame()
    {
        Rig r;
        r.hello();
        r.send("subscribe", "s", {{"topics", QJsonArray{"snapshot"}}});
        r.flush();
        ++r.p.userOffsetMs;
        r.publish();
        r.engine.pump(0);
        r.sinks[0].limit = 1;
        r.engine.write(r.client, 0);
        r.send("unsubscribe", "u");
        r.sinks[0].limit = 65536;
        auto m = r.flush();
        QCOMPARE(m.size(), 2);
        QCOMPARE(m[0]["kind"].toString(), QString("event"));
        QCOMPARE(m[1]["id"].toString(), QString("u"));
    }
    void changedRevisionRejectsQueuedPages_data()
    {
        QTest::addColumn<bool>("partial");
        QTest::newRow("unsent") << false;
        QTest::newRow("partial") << true;
    }
    void changedRevisionRejectsQueuedPages()
    {
        QFETCH(bool, partial);
        Rig r;
        r.hello();
        r.send("getLyricsSlice", "page", sliceExtra(r.p));
        if (partial) {
            r.sinks[0].limit = 1;
            r.engine.write(r.client, 0);
        }
        auto d = std::make_shared<PlaybackLyricDocument>(*r.document);
        ++d->revision;
        r.document = d;
        ++r.p.lyricsRevision;
        r.publish();
        r.engine.pump(0);
        if (partial) {
            QVERIFY(!r.engine.isOpen(r.client));
            QCOMPARE(r.engine.lastClose(), ObservationClose::StalePartial);
            QCOMPARE(r.sinks[0].delivered.size(), 1);
            return;
        }
        const auto m = r.flush();
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0]["error"].toObject()["code"].toString(), QString("StaleView"));
    }
    void retainedPrefixesAreIncludedInBackpressure()
    {
        Rig r;
        r.hello();
        auto d = std::make_shared<PlaybackLyricDocument>(*r.document);
        ++d->revision;
        d->lyrics.original.standard.lines[0].text = QString(160000, QChar(1));
        r.document = d;
        r.p.lyricsRevision = d->revision;
        r.publish();
        while (r.engine.encodingPending())
            r.engine.pump(0);
        r.engine.pump(0); // consume the newly published document if no job was previously pending
        while (r.engine.encodingPending())
            r.engine.pump(0);
        r.sinks[0].immediate = false;
        r.send("getLyricsSlice", "a", sliceExtra(r.p));
        r.send("getLyricsSlice", "b", sliceExtra(r.p));
        const auto retained = r.engine.applicationBytes(r.client);
        QVERIFY(retained > 1800000);
        for (int i = 0; i < 20; ++i)
            r.engine.write(r.client, 0);
        QCOMPARE(r.engine.applicationBytes(r.client), retained); // active prefix NOT subtracted
        QCOMPARE(r.engine.applicationBytes(r.client) + r.sinks[0].pendingBytes(),
                 qint64(PlaybackObservationConnections::ClientBytes));
        for (int i = 0; i < 80; ++i) {
            r.sinks[0].drain(65536);
            r.engine.write(r.client, 0);
            QVERIFY(r.engine.isOpen(r.client));
            QVERIFY(r.engine.applicationBytes(r.client) + r.sinks[0].pendingBytes() <=
                    PlaybackObservationConnections::ClientBytes);
        }
        r.sinks[0].drain(PlaybackObservationConnections::ClientBytes);
        const auto m = r.sinks[0].messages();
        QCOMPARE(m.size(), 2);
        QCOMPARE(m[0]["id"].toString(), QString("a"));
        QCOMPARE(m[1]["id"].toString(), QString("b"));
        QCOMPARE(m[0]["lines"].toArray()[0].toObject()["text"].toString(),
                 d->lyrics.original.standard.lines[0].text);
    }
    void privacyRetiresBeforeWakeAndDuringWrite()
    {
        Rig r;
        r.hello();
        r.send("getLyricsSlice", "page", sliceExtra(r.p));
        r.box->retire(r.p.binding);
        // Do not run pump/wake: admission itself must reject the old fence.
        r.engine.write(r.client, 0);
        QVERIFY(!r.engine.isOpen(r.client));
        QVERIFY(r.sinks[0].delivered.isEmpty());
        QVERIFY(!r.box->publish(r.p, r.document)); // phase-back cannot rebind old epoch
        const auto old = r.p;
        PlaybackObservation empty;
        empty.binding = {old.binding.instanceId, QUuid::createUuid(), {}};
        empty.sequence = old.sequence + 1;
        empty.sampleMonoMs = old.sampleMonoMs;
        QVERIFY(!r.box->publish(empty)); // a historical value cannot change the active view
        QVERIFY(r.box->beginView(empty.binding));
        QVERIFY(r.box->publish(empty));
        r.p = empty;
        auto late = old;
        late.sequence = 100;
        QVERIFY(!r.box->publish(late, r.document));
        auto newClient = r.engine.open(r.sinks[1], 0);
        QVERIFY(newClient);
        r.engine.receive(newClient, request("hello", "new"), 0);
        r.engine.pump(0);
        r.sinks[1].onWrite = [&] { r.box->retire(r.p.binding); };
        r.engine.write(newClient, 0);
        QVERIFY(!r.engine.isOpen(newClient));
        QVERIFY(!r.sinks[1].delivered.isEmpty()); // already admitted bytes cannot be revoked
    }
    void barrierBeforeFirstSampleAndOlderSourceRejected()
    {
        auto b = std::make_shared<ObservationMailbox>();
        auto p = test::observationValue();
        p.sequence = 1;
        b->retire(p.binding);
        QVERIFY(!b->publish(p));
        const auto old = p.binding;
        p.binding.viewEpoch = QUuid::createUuid();
        QVERIFY(b->beginView(p.binding));
        QVERIFY(b->publish(p));
        QVERIFY(!b->publish(p));
        --p.sequence;
        QVERIFY(!b->publish(p));
        QCOMPARE(b->take()->snapshot.sequence, quint64(1));
        p.binding = old;
        p.sequence = 100;
        QVERIFY(!b->publish(p));
    }
    void newLoadCannotReceiveAnOldSlice()
    {
        Rig r;
        r.hello();
        const auto origin = sliceExtra(r.p);
        r.p.binding.trackId = QUuid::createUuid();
        auto d = std::make_shared<PlaybackLyricDocument>(*r.document);
        d->binding = r.p.binding;
        r.document = d;
        r.publish();
        r.engine.pump(0);
        r.send("getLyricsSlice", "old", origin);
        const auto m = r.flush();
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0]["error"].toObject()["code"].toString(), QString("StaleView"));
    }
    void slowClientIgnoresNewEnqueuesAsProgress()
    {
        Rig r;
        r.hello();
        r.sinks[0].immediate = false;
        for (int i = 0; i < 5; ++i) {
            r.now = i * 1000;
            r.send("getSnapshot", QString::number(i));
            r.flush();
            QVERIFY(r.engine.isOpen(r.client));
        }
        r.engine.pump(5000);
        QVERIFY(!r.engine.isOpen(r.client));
        QCOMPARE(r.engine.lastClose(), ObservationClose::Timeout);
        QVERIFY(r.sinks[0].queued.isEmpty());
    }
    void drainedTransportReallyExtendsSlowDeadline()
    {
        Rig r;
        r.hello();
        r.sinks[0].immediate = false;
        r.send("getSnapshot", "s");
        r.flush();
        r.engine.pump(4999);
        QVERIFY(r.engine.isOpen(r.client));
        r.sinks[0].drain(1);
        r.engine.pump(4999);
        r.engine.pump(9998);
        QVERIFY(r.engine.isOpen(r.client));
        r.engine.pump(9999);
        QVERIFY(!r.engine.isOpen(r.client));
    }
    void eightClientsAndNinthRejected()
    {
        Rig r;
        for (int i = 1; i < 8; ++i)
            QVERIFY(r.engine.open(r.sinks[size_t(i)], 0));
        QCOMPARE(r.engine.clientCount(), 8);
        QCOMPARE(r.engine.open(r.sinks[8], 0), quint64(0));
        r.engine.close();
        QCOMPARE(r.engine.clientCount(), 0);
        for (int i = 0; i < 8; ++i)
            QCOMPARE(r.sinks[size_t(i)].closes, 1);
    }
    void backpressureAndTransportFailure()
    {
        Rig r;
        r.hello();
        r.sinks[0].queued.resize(PlaybackObservationConnections::ClientBytes);
        r.send("getSnapshot", "s");
        QVERIFY(!r.engine.isOpen(r.client));
        QCOMPARE(r.engine.lastClose(), ObservationClose::Backpressure);
        Rig bad;
        bad.hello();
        bad.sinks[0].fail = true;
        bad.send("getSnapshot", "s");
        bad.engine.write(bad.client, 0);
        QVERIFY(!bad.engine.isOpen(bad.client));
        QCOMPARE(bad.engine.lastClose(), ObservationClose::Transport);
    }
    void clockAndDestructionNeverCommandOwner()
    {
        Rig r;
        r.hello();
        r.engine.pump(10);
        r.engine.pump(9);
        QVERIFY(!r.engine.isOpen(r.client));
        QCOMPARE(r.engine.lastClose(), ObservationClose::Clock);
        QCOMPARE(r.engine.open(r.sinks[1], 10), quint64(0));
        Sink sink;
        auto b = std::make_shared<ObservationMailbox>();
        b->publish(test::observationValue());
        {
            PlaybackObservationConnections e(b);
            QVERIFY(e.open(sink, 0));
        }
        QCOMPARE(sink.closes, 1);
        QVERIFY(b->take());
    }
    void incrementalMatchesPureFactoryAndCanBeCanceled()
    {
        auto d = std::make_shared<PlaybackLyricDocument>(test::observationDocument());
        ++d->revision; // New content needs a new immutable document revision.
        d->lyrics.original.enhanced.lines[0].words = QList<LyricWord>(1200, LyricWord{0, 1, "x"});
        PlaybackLyricsEncoder job(d);
        job.step(1);
        QVERIFY(!job.done());
        QVERIFY(!job.result().document);
        int turns = 1;
        while (!job.done() && turns < 2000) {
            job.step(1);
            ++turns;
        }
        QVERIFY(job.done());
        QVERIFY(turns > 1200);
        QCOMPARE(job.result().status, ObservationContentStatus::Ready);
        QCOMPARE(job.result().document->normalizedBytes(),
                 Codec::prepare(*d).document->normalizedBytes());
        Rig r;
        r.document = d;
        r.p.lyricsRevision = d->revision;
        test::observationSummaries(r.p, d->lyrics);
        r.publish();
        r.engine.pump(0);
        QVERIFY(r.engine.encodingPending());
        r.box->retire(r.p.binding);
        r.engine.pump(0);
        QVERIFY(!r.engine.encodingPending());
        QCOMPARE(r.engine.clientCount(), 0);
    }
    void producerThreadOnlySingleLatestWake()
    {
        auto b = std::make_shared<ObservationMailbox>();
        auto p = test::observationValue();
        p.sequence = 1;
        std::thread producer([&] {
            for (quint64 i = 1; i <= 1000; ++i) {
                p.sequence = i;
                b->publish(p);
            }
            b->retire(p.binding);
        });
        producer.join();
        QVERIFY(b->wakePending());
        QVERIFY(!b->take());
        QVERIFY(!b->wakePending());
        QVERIFY(!b->publish(p));
    }
};
QTEST_GUILESS_MAIN(PlaybackObservationConnectionTest)
#include "playback_observation_connection_test.moc"
