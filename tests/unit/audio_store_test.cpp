#include "../support/fake_range_http.h"
#include "infrastructure/media/audio_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <future>
using namespace cirrus;
using namespace domain;
using media::AudioStore;
using media::AudioStream;
namespace
{
constexpr qint64 Chunk = 256 * 1024;

QByteArray noise(qint64 bytes, quint32 seed)
{
    QByteArray data(bytes, Qt::Uninitialized);
    QRandomGenerator random(seed);
    random.fillRange(reinterpret_cast<quint32*>(data.data()), bytes / 4);
    return data;
}
MediaSource source(const QByteArray& body, bool withMd5 = true, const char* name = "a.mp3")
{
    MediaSource s{QUrl(QStringLiteral("https://m801.music.126.net/") + QLatin1String(name)),
                  "exhigh", "mp3", body.size(), QDeadlineTimer(60000)};
    if (withMd5)
        s.md5 = QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex();
    return s;
}
// Reads on another thread (reads block) while the test spins the event loop.
std::future<QByteArray> readAsync(std::shared_ptr<AudioStream> stream, qint64 from = 0,
                                  qint64 bytes = -1)
{
    return std::async(std::launch::async, [stream, from, bytes] {
        QByteArray out;
        const qint64 end = bytes < 0 ? stream->size() : from + bytes;
        char buffer[48 * 1024];
        for (qint64 at = from; at < end;) {
            const auto got = stream->read(at, buffer, std::min<qint64>(sizeof buffer, end - at));
            if (got <= 0)
                return got < 0 ? QByteArray("FAILED") : out;
            out.append(buffer, got);
            at += got;
        }
        return out;
    });
}
bool ready(std::future<QByteArray>& f)
{
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
} // namespace

class AudioStoreTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_root;

    std::unique_ptr<AudioStore> store(test::RangeServer& server, qint64 capacity = 64 << 20)
    {
        AudioStore::Options options;
        options.root = m_root;
        options.capacityBytes = capacity;
        options.retryDelayMs = 10;
        options.refreshTimeoutMs = 300;
        auto s =
            std::make_unique<AudioStore>(options, std::make_unique<test::RangeManager>(server));
        s->setPartition(QStringLiteral("account-a"));
        return s;
    }
    // Waits for the stream's file to be verified and the handle released.
    static void settle(AudioStore& store, qint64 usage)
    {
        QTRY_COMPARE_WITH_TIMEOUT(store.usage(), usage, 5000);
    }

  private slots:
    void init()
    {
        // Under the build tree: /tmp fails the private-path check (world-writable).
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() +
                                                QStringLiteral("/audio-store-XXXXXX"));
        QVERIFY(m_dir->isValid());
        m_root = m_dir->path() + QStringLiteral("/audio");
    }
    void cleanup() { m_dir.reset(); }

    void streamsThenPlaysFromCache()
    {
        test::RangeServer server{noise(Chunk * 4 + 1000, 1)};
        auto s = store(server);
        QVERIFY(s->persistent());
        auto stream = s->open(source(server.body));
        QVERIFY(stream);
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 5000);
        stream.reset();
        settle(*s, server.body.size());
        const auto requests = server.ranges.size();

        // Again: from the disk, no request.
        auto again = s->open(source(server.body));
        auto second = readAsync(again);
        QTRY_VERIFY_WITH_TIMEOUT(ready(second), 5000);
        QCOMPARE(second.get(), server.body);
        QCOMPARE(server.ranges.size(), requests);
    }

    void seekFetchesThereFirst()
    {
        test::RangeServer server{noise(Chunk * 16, 2)};
        server.intervalMs = 5;
        server.blockBytes = 32 * 1024;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        const qint64 at = Chunk * 12 + 100;
        auto read = readAsync(stream, at, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body.mid(at, 10000));
        QVERIFY(server.ranges.contains("bytes=" + QByteArray::number(Chunk * 12) + '-' +
                                       QByteArray::number(server.body.size() - 1)));
        // The rest fills in behind, and the whole file checks out.
        QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 10000);
        auto all = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(all), 5000);
        QCOMPARE(all.get(), server.body);
        stream.reset();
        settle(*s, server.body.size());
    }

    void serverIgnoringRangeStillWorks()
    {
        test::RangeServer server{noise(Chunk * 6, 3)};
        server.honorRange = false;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream, Chunk * 4, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body.mid(Chunk * 4, 5000));
    }

    void wrongContentRangeFails()
    {
        test::RangeServer server{noise(Chunk * 2, 4)};
        server.contentRange = "bytes 0-99/99999";
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
        QCOMPARE(stream->failure(), std::optional(PlaybackError::InvalidResponse));
    }

    void refusedAddressAsksForANewOne()
    {
        test::RangeServer server{noise(Chunk * 3, 5)};
        server.statuses = {403};
        auto s = store(server);
        QSignalSpy needed(s.get(), &AudioStore::urlNeeded);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_COMPARE_WITH_TIMEOUT(needed.size(), 1, 5000);
        s->refresh(needed.first().first().toULongLong(),
                   QUrl(QStringLiteral("https://m802.music.126.net/fresh.mp3")),
                   QDeadlineTimer(60000));
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QCOMPARE(server.urls.last().host(), QStringLiteral("m802.music.126.net"));
    }

    void noNewAddressExpires()
    {
        test::RangeServer server{noise(Chunk * 3, 6)};
        server.statuses = {403};
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
        QCOMPARE(stream->failure(), std::optional(PlaybackError::Expired));
    }

    void retriesThenGivesUp()
    {
        test::RangeServer server{noise(Chunk * 2, 7)};
        server.statuses = {500, 500};
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);

        test::RangeServer down{noise(Chunk * 2, 8)};
        down.statuses = {500, 500, 500, 500};
        auto t = store(down);
        auto failing = t->open(source(down.body));
        auto lost = readAsync(failing);
        QTRY_VERIFY_WITH_TIMEOUT(ready(lost), 5000);
        QCOMPARE(lost.get(), QByteArray("FAILED"));
        QCOMPARE(failing->failure(), std::optional(PlaybackError::Network));
    }

    void wrongMd5IsNotKept()
    {
        test::RangeServer server{noise(Chunk * 2, 9)};
        auto s = store(server);
        auto bad = source(server.body);
        bad.md5 = QByteArray(32, 'a');
        auto stream = s->open(bad);
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body); // still plays
        stream.reset();
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), 0, 5000);
        const auto requests = server.ranges.size();
        auto again = s->open(bad);
        auto second = readAsync(again);
        QTRY_VERIFY_WITH_TIMEOUT(ready(second), 5000);
        QVERIFY(server.ranges.size() > requests); // downloaded again
    }

    void evictsTheLeastRecentlyUsed()
    {
        test::RangeServer server{noise(Chunk * 4, 10)};
        auto s = store(server, Chunk * 10); // room for two of three files
        QList<MediaSource> sources;
        for (int i = 0; i < 3; ++i) {
            server.body = noise(Chunk * 4, 20 + i);
            sources.append(source(server.body, true,
                                  i == 0   ? "1.mp3"
                                  : i == 1 ? "2.mp3"
                                           : "3.mp3"));
            auto stream = s->open(sources.last());
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 5000);
            stream.reset();
            QTest::qWait(20); // distinct last-used times
            QTRY_VERIFY_WITH_TIMEOUT(s->usage() % (Chunk * 4) == 0, 5000);
        }
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), Chunk * 8, 5000);
        // The first one went: opening it downloads again.
        const auto requests = server.ranges.size();
        server.body = noise(Chunk * 4, 20);
        auto first = s->open(sources.first());
        auto read = readAsync(first);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QVERIFY(server.ranges.size() > requests);
    }

    void partitionsAreSeparate()
    {
        test::RangeServer server{noise(Chunk * 2, 11)};
        auto s = store(server);
        {
            auto stream = s->open(source(server.body));
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        }
        settle(*s, server.body.size());
        s->setPartition(QStringLiteral("account-b"));
        auto requests = server.ranges.size();
        {
            auto stream = s->open(source(server.body));
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        }
        QVERIFY(server.ranges.size() > requests);
        settle(*s, server.body.size() * 2);
        s->setPartition(QStringLiteral("account-a"));
        requests = server.ranges.size();
        {
            auto stream = s->open(source(server.body));
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        }
        QCOMPARE(server.ranges.size(), requests);
        s->clear(); // this partition only
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), server.body.size(), 5000);
        s->clear(true);
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), 0, 5000);
    }

    void cancelWakesABlockedRead()
    {
        test::RangeServer server{noise(Chunk * 8, 12)};
        server.intervalMs = 10000; // the first block, then a long wait
        server.blockBytes = 1024;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream, Chunk * 2, 100);
        QTest::qWait(100);
        QVERIFY(!ready(read));
        stream->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 1000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
    }

    void interruptWakesOneRead()
    {
        test::RangeServer server{noise(Chunk * 8, 16)};
        server.intervalMs = 10000; // the first block, then a long wait
        server.blockBytes = 1024;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto blocked = readAsync(stream, Chunk * 4, 100);
        QTest::qWait(100);
        stream->interrupt();
        QTRY_VERIFY_WITH_TIMEOUT(ready(blocked), 1000);
        QCOMPARE(blocked.get(), QByteArray("FAILED"));
        // Unlike cancel(), later reads still work.
        auto head = readAsync(stream, 0, 512);
        QTRY_VERIFY_WITH_TIMEOUT(ready(head), 1000);
        QCOMPARE(head.get(), server.body.left(512));
    }

    void prefetchedHeadIsReused()
    {
        test::RangeServer server{noise(Chunk * 8, 13)};
        auto s = store(server);
        s->prefetch(source(server.body), Chunk * 2);
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), Chunk * 2, 5000);
        server.ranges.clear();
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QVERIFY(!server.ranges.isEmpty());
        QVERIFY(server.ranges.first().startsWith("bytes=" + QByteArray::number(Chunk * 2) + '-'));
    }

    void withoutMd5NothingIsKept()
    {
        test::RangeServer server{noise(Chunk * 2, 14)};
        auto s = store(server);
        auto stream = s->open(source(server.body, false));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        stream.reset();
        QTest::qWait(50);
        QCOMPARE(s->usage(), 0);
    }

    void refusesUnsafeOrHugeSources()
    {
        test::RangeServer server{noise(1024, 15)};
        auto s = store(server);
        auto unsafe = source(server.body);
        unsafe.url = QUrl(QStringLiteral("https://example.com/a.mp3"));
        PlaybackError error{};
        QVERIFY(!s->open(unsafe, &error));
        QCOMPARE(error, PlaybackError::UnsafeSource);
        auto huge = source(server.body);
        huge.size = 600LL * 1024 * 1024;
        QVERIFY(!s->open(huge, &error));
        QCOMPARE(error, PlaybackError::TooLarge);
    }
};
QTEST_GUILESS_MAIN(AudioStoreTest)
#include "audio_store_test.moc"
