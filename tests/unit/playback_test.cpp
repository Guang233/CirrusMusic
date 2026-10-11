#include "../support/fake_playback.h"
#include "application/playback/playback_coordinator.h"
#include <QtTest>
using namespace cirrus;
using namespace domain;
using Kind = application::PlaybackEvent::Kind;
namespace
{
StreamSource source(QString id)
{
    return {{id},       QUrl("https://m802.music.126.net/synthetic.mp3"),
            "standard", "mp3",
            100,        QDeadlineTimer(60000)};
}
} // namespace
class PlaybackTest : public QObject
{
    Q_OBJECT
  private slots:
    void initialPauseIsAtomic()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}, TransportIntent::Pause));
        resolver.finish(0, source("1"));
        emit backend.event({token, Kind::Ready});
        QCOMPARE(backend.plays, 0);
        QCOMPARE(player.snapshot().state, PlaybackState::Paused);
    }
    void pauseAndCoalescedSeek()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}));
        QVERIFY(player.pause());
        QVERIFY(player.seek(10));
        QVERIFY(player.seek(8000));
        resolver.finish(0, source("1"));
        QCOMPARE(backend.loads.size(), 1);
        QCOMPARE(backend.plays, 0);
        emit backend.event({token, Kind::Duration, {}, 5000});
        emit backend.event({token, Kind::Seekable, {}, 1});
        emit backend.event({token, Kind::Ready});
        QCOMPARE(backend.plays, 0);
        QCOMPARE(backend.seeks, QList<qint64>{4999});
        QCOMPARE(player.snapshot().state, PlaybackState::Paused);
        QVERIFY(player.resume());
        QCOMPARE(backend.plays, 1);
        emit backend.event({token, Kind::State, PlaybackState::Playing});
        QCOMPARE(player.snapshot().state, PlaybackState::Playing);
        QVERIFY(player.pause());
        QCOMPARE(player.snapshot().state, PlaybackState::Playing); // acceptance isn't effect
        emit backend.event(
            {token, Kind::State, PlaybackState::Playing}); // stale transport intention
        QCOMPARE(player.snapshot().state, PlaybackState::Playing);
        emit backend.event({token, Kind::State, PlaybackState::Paused});
        QCOMPARE(player.snapshot().state, PlaybackState::Paused);
    }
    void outOfOrderSourcesAndRetiredMedia()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const auto context = session.snapshot().token;
        QVERIFY(player.start({context, 1}, {{"1"}}));
        resolver.finish(0, source("1")); // A was actually loaded
        QVERIFY(player.start({context, 2}, {{"2"}}));
        QVERIFY(player.start({context, 3}, {{"3"}}));
        resolver.finish(2, source("3"));
        resolver.finish(1, source("2"));
        resolver.finish(0, source("1"));
        QCOMPARE(backend.loads.size(), 2);
        QCOMPARE(backend.loads.last().generation, 3);
        emit backend.event({{context, 1}, Kind::Failure, {}, 0, {PlaybackError::Decode}});
        emit backend.event({{context, 1}, Kind::Ended});
        emit backend.event({{context, 1}, Kind::Position, {}, 333});
        QCOMPARE(player.snapshot().song.value, "3");
        QCOMPARE(player.snapshot().positionMs, 0);
        emit backend.event({{context, 3}, Kind::Ready});
        emit backend.event({{context, 3}, Kind::Ended});
        QCOMPARE(player.snapshot().state, PlaybackState::Ended);
        const auto stops = backend.stops;
        emit backend.event({{context, 3}, Kind::Ended});
        QCOMPARE(backend.stops, stops);
        QVERIFY(!player.resume()); // explicit play opens a new generation, no old-source replay
    }
    void stopAndSessionIsolation()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}));
        player.stop();
        resolver.finish(0, source("1"));
        QVERIFY(backend.loads.isEmpty());
        QVERIFY(player.start({token.session, 2}, {{"2"}}));
        static_cast<void>(session.beginAuthentication());
        resolver.finish(1, source("2"));
        QVERIFY(backend.loads.isEmpty());
        QCOMPARE(player.snapshot().state, PlaybackState::Empty);
        QVERIFY(!player.start({token.session, 3}, {{"3"}}));
    }
    void permissionExpiryAndErrorAreBounded()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const auto context = session.snapshot().token;
        QVERIFY(player.start({context, 1}, {{"1"}}));
        resolver.finish(0, PlaybackFailure{PlaybackError::Unavailable, 404});
        QCOMPARE(player.snapshot().state, PlaybackState::Failed);
        QCOMPARE(resolver.requests.size(), 1);
        QVERIFY(backend.loads.isEmpty());
        QVERIFY(player.start({context, 2}, {{"2"}}));
        auto expired = source("2");
        expired.validUntil = QDeadlineTimer(0);
        resolver.finish(1, expired);
        QCOMPARE(player.snapshot().failure->error, PlaybackError::Expired);
        QVERIFY(backend.loads.isEmpty());
        QVERIFY(player.start({context, 3}, {{"3"}}));
        resolver.finish(2, source("WRONG"));
        QCOMPARE(player.snapshot().failure->error, PlaybackError::InvalidResponse);
    }
    void cannotReuseLoadIdentity()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}));
        QVERIFY(!player.start(token, {{"2"}}));
        player.stop();
        QVERIFY(!player.start(token, {{"3"}}));
        QVERIFY(player.start({token.session, 2}, {{"3"}}));
        QCOMPARE(resolver.requests.size(), 2);
    }
    void reentrantStopDuringPublication()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        connect(&player, &application::PlaybackCoordinator::changed, &player,
                [&](PlaybackSnapshot value) {
                    if (value.state == PlaybackState::Loading)
                        player.stop();
                });
        QVERIFY(player.start({session.snapshot().token, 1}, {{"1"}}));
        resolver.finish(0, source("1"));
        QVERIFY(backend.loads.isEmpty());
        QCOMPARE(player.snapshot().state, PlaybackState::Empty);
    }
};
QTEST_GUILESS_MAIN(PlaybackTest)
#include "playback_test.moc"
