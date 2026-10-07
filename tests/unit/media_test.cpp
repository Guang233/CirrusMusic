#include "../support/fake_http.h"
#include "infrastructure/media/media_buffer.h"
#include "infrastructure/media/media_source_policy.h"
#include "infrastructure/media/qt_playback_backend.h"
#include <QDataStream>
#include <QEventLoop>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>
using namespace cirrus;
using namespace domain;
namespace
{
class GetManager final : public QNetworkAccessManager
{
  public:
    QList<test::Plan> plans;
    QList<QNetworkRequest> requests;
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice*) override
    {
        Q_ASSERT(op == GetOperation);
        requests.append(request);
        return new test::FakeReply(request, plans.isEmpty() ? test::Plan{} : plans.takeFirst(),
                                   this);
    }
};
MediaSource source(qint64 bytes = 8)
{
    return {QUrl("https://m802.music.126.net/synthetic.mp3"), "standard", "mp3", bytes,
            QDeadlineTimer(60000)};
}
const LoadToken Token{{QUuid::createUuid(), 1}, 1};
QByteArray wave()
{
    QByteArray result;
    QDataStream out(&result, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 samples = 8000 * 3;
    out.writeRawData("RIFF", 4);
    out << quint32(36 + samples * 2);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2)
        << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(samples * 2);
    result.append(QByteArray(samples * 2, '\0')); // silence, never copyrighted fixture audio
    return result;
}
} // namespace
class MediaTest : public QObject
{
    Q_OBJECT
  private slots:
    void sourcePolicy_data()
    {
        QTest::addColumn<QString>("url");
        for (const auto* url :
             {"file:///etc/passwd", "http://m802.music.126.net/a", "https://localhost/a",
              "https://m802.music.126.net.evil.example/a", "https://evil.example/music.126.net/a",
              "https://user:password@m802.music.126.net/a", "https://m802.music.126.net:8443/a",
              "https://m802.music.126.net/a#fragment", "https://127.0.0.1/a",
              "https://m802.music.126.net/"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }
    void sourcePolicy()
    {
        QFETCH(QString, url);
        QVERIFY(!media::approvedMediaUrl(QUrl(url)));
    }
    void boundedBufferAndHeaders()
    {
        auto manager = std::make_unique<GetManager>();
        auto* raw = manager.get();
        manager->plans.append({"ID3audio",
                               200,
                               0,
                               QNetworkReply::NoError,
                               {{"Content-Length", "8"}, {"Set-Cookie", "secret=synthetic"}}});
        media::MediaBuffer buffer(std::move(manager));
        QSignalSpy ready(&buffer, &media::MediaBuffer::ready);
        buffer.start(source(), Token);
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(buffer.device());
        QCOMPARE(buffer.device()->readAll(), "ID3audio");
        QCOMPARE(raw->requests.size(), 1);
        const auto request = raw->requests.first();
        QVERIFY(!request.hasRawHeader("Cookie"));
        QVERIFY(!request.hasRawHeader("Authorization"));
        QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
                 int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(),
                 int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 int(QNetworkRequest::ManualRedirectPolicy));
        buffer.cancel();
        QVERIFY(!buffer.device());
    }
    void redirectAndDowngrade()
    {
        auto manager = std::make_unique<GetManager>();
        auto* raw = manager.get();
        manager->plans = {
            {"", 302, 0, QNetworkReply::NoError, {{"Location", "https://m801.music.126.net/next"}}},
            {"ID3audio", 200, 0, QNetworkReply::NoError, {}}};
        media::MediaBuffer buffer(std::move(manager));
        QSignalSpy ready(&buffer, &media::MediaBuffer::ready);
        QSignalSpy failed(&buffer, &media::MediaBuffer::failed);
        buffer.start(source(), Token);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(raw->requests.size(), 2);
        raw->plans = {
            {"", 302, 0, QNetworkReply::NoError, {{"Location", "http://m801.music.126.net/next"}}}};
        buffer.start(source(), {Token.session, 2});
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(raw->requests.size(), 3);
        QVERIFY(!buffer.device());
        QCOMPARE(qvariant_cast<PlaybackFailure>(failed.first()[1]).error,
                 PlaybackError::UnsafeSource);
    }
    void redirectLoopIsBounded()
    {
        auto manager = std::make_unique<GetManager>();
        auto* raw = manager.get();
        for (int i = 0; i < 5; ++i)
            manager->plans.append({"", 302, 0, QNetworkReply::NoError, {{"Location", "/loop"}}});
        media::MediaBuffer buffer(std::move(manager));
        QSignalSpy failed(&buffer, &media::MediaBuffer::failed);
        buffer.start(source(), Token);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(raw->requests.size(), 4);
    }
    void limitsTruncationTlsAndExpiry()
    {
        auto manager = std::make_unique<GetManager>();
        auto* raw = manager.get();
        media::MediaBuffer buffer(std::move(manager));
        QSignalSpy failed(&buffer, &media::MediaBuffer::failed);
        auto large = source(media::MediaBuffer::MaximumBytes + 1);
        buffer.start(large, Token);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(raw->requests.isEmpty());
        raw->plans.append({"short", 200, 0, QNetworkReply::NoError, {}});
        buffer.start(source(), {Token.session, 2});
        QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(qvariant_cast<PlaybackFailure>(failed.last()[1]).error,
                 PlaybackError::InvalidResponse);
        raw->plans.append({"123456789", 200, 0, QNetworkReply::NoError, {}});
        buffer.start(source(), {Token.session, 3});
        QTRY_COMPARE(failed.size(), 3);
        QCOMPARE(qvariant_cast<PlaybackFailure>(failed.last()[1]).error, PlaybackError::TooLarge);
        raw->plans.append({"", 0, 0, QNetworkReply::SslHandshakeFailedError, {}});
        buffer.start(source(), {Token.session, 4});
        QTRY_COMPARE(failed.size(), 4);
        QCOMPARE(qvariant_cast<PlaybackFailure>(failed.last()[1]).error, PlaybackError::Network);
        raw->plans.append({"", 200, -1, QNetworkReply::NoError, {}});
        auto shortLived = source();
        shortLived.validUntil = QDeadlineTimer(20);
        buffer.start(shortLived, {Token.session, 5});
        QTRY_COMPARE(failed.size(), 5);
        QCOMPARE(qvariant_cast<PlaybackFailure>(failed.last()[1]).error, PlaybackError::Expired);
        QVERIFY(!buffer.device());
    }
    void cancellationAndPlaylistRejection()
    {
        auto manager = std::make_unique<GetManager>();
        auto* raw = manager.get();
        manager->plans.append({"ID3audio", 200, -1, QNetworkReply::NoError, {}});
        media::MediaBuffer buffer(std::move(manager));
        QSignalSpy ready(&buffer, &media::MediaBuffer::ready);
        QSignalSpy failed(&buffer, &media::MediaBuffer::failed);
        buffer.start(source(), Token);
        QTRY_COMPARE(raw->requests.size(), 1);
        buffer.cancel();
        QCoreApplication::processEvents();
        QVERIFY(ready.isEmpty());
        QVERIFY(failed.isEmpty());
        auto playlistManager = std::make_unique<GetManager>();
        playlistManager->plans.append({"#EXTM3U\n", 200, 0, QNetworkReply::NoError, {}});
        media::QtPlaybackBackend backend(std::move(playlistManager));
        QSignalSpy events(&backend, &application::IPlaybackBackend::event);
        backend.load(source(), Token);
        QTRY_COMPARE(events.size(), 1);
        const auto event = qvariant_cast<application::PlaybackEvent>(events.first()[0]);
        QCOMPARE(event.kind, application::PlaybackEvent::Kind::Failure);
        QCOMPARE(event.failure.error, PlaybackError::UnsupportedFormat);
    }
    void silentQtSmoke()
    {
        if (!qEnvironmentVariableIsSet("CIRRUS_AUDIO_SMOKE"))
            QSKIP("Opt-in desktop Qt audio smoke: CIRRUS_AUDIO_SMOKE=1");
        // Exercise the REAL Qt adapter on a worker, not just a Core-only fake player.
        // Result fields are read only after joining this test-owned thread.
        bool ready = false, paused = false, seekAcknowledged = false, failure = false,
             wrongToken = false;
        std::unique_ptr<QThread> worker(QThread::create([&] {
            QEventLoop loop;
            auto manager = std::make_unique<GetManager>();
            const auto data = wave();
            manager->plans.append({data, 200, 0, QNetworkReply::NoError, {}});
            media::QtPlaybackBackend backend(std::move(manager));
            backend.setVolume(0);
            connect(&backend, &application::IPlaybackBackend::event, &loop,
                    [&](application::PlaybackEvent event) {
                        using Kind = application::PlaybackEvent::Kind;
                        wrongToken |= event.token != Token;
                        if (event.kind == Kind::Failure) {
                            failure = true;
                            loop.quit();
                        }
                        if (event.kind == Kind::State && event.state == PlaybackState::Paused)
                            paused = true;
                        if (event.kind == Kind::Ready) {
                            ready = true;
                            backend.play();
                            QTimer::singleShot(250, &loop, [&] {
                                backend.pause();
                                backend.seek(1500);
                            });
                        }
                        if (event.kind == Kind::Position && event.value >= 1500) {
                            seekAcknowledged = true;
                            QTimer::singleShot(50, &loop, &QEventLoop::quit);
                        }
                    });
            auto input = source(data.size());
            input.format = "wav";
            backend.load(input, Token);
            QTimer::singleShot(8000, &loop, &QEventLoop::quit);
            loop.exec();
            backend.stop();
        }));
        QSignalSpy finished(worker.get(), &QThread::finished);
        worker->start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 12000);
        worker->wait(); // test-only join AFTER finished; product facade never waits
        QVERIFY(!failure);
        QVERIFY(!wrongToken);
        QVERIFY(ready);
        QVERIFY(paused);
        QVERIFY(seekAcknowledged);
    }
};
QTEST_GUILESS_MAIN(MediaTest)
#include "media_test.moc"
