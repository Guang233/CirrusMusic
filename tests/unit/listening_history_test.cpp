#include "../support/fake_playback.h"
#include "application/music/personal_fm_source.h"
#include "application/playback/listening_history.h"
#include "application/playback/listening_session.h"
#include "application/playback/observed_playback.h"
#include "application/playback/playback_coordinator.h"
#include <QSignalSpy>
#include <QtTest>
#include <limits>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::test;
namespace
{
PlaybackSnapshot snapshot(LoadToken token, qint64 played,
                          PlaybackState state = PlaybackState::Playing)
{
    PlaybackSnapshot s;
    s.token = token;
    s.song = {"1"};
    s.state = state;
    s.intent = TransportIntent::Play;
    s.positionMs = 100000; // deliberately unrelated to listening duration
    s.observed = ObservedPlayback{played};
    s.actualQuality = "standard";
    s.preview = PreviewRange{100000, 130000};
    return s;
}
struct Harness {
    BackendFacade f;
    PersonalFmSource fm{f};
    ListeningSession listening{f, fm};
    QList<MusicCommand> queries;
    Harness()
    {
        QObject::connect(&f, &BackendFacade::musicRequested, &f,
                         [this](MusicCommand c) { queries.append(c); });
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Authenticated, "101"};
        f.deliver(e);
    }
    void progress(qint64 played, PlaybackState state = PlaybackState::Playing)
    {
        auto s = snapshot(f.playback().token, played, state);
        s.song = f.playback().song;
        f.deliverPlayback({0, s});
    }
    void batch()
    {
        for (auto it = queries.crbegin(); it != queries.crend(); ++it) {
            if (!std::holds_alternative<PersonalFmQuery>(it->query))
                continue;
            PersonalFmBatch b;
            Song song;
            song.id = {"3"};
            b.songs.append(song);
            const auto command = *it;
            f.deliverMusic({command.id, command.generation, command.context, b});
            return;
        }
        QFAIL("no FM query");
    }
};
} // namespace
class ListeningHistoryTest : public QObject
{
    Q_OBJECT
  private slots:
    void meterRequiresTwoEligibleSamples()
    {
        ObservedPlaybackMeter m;
        QCOMPARE(m.sample(100000, 0, true).playedMs, 0);
        QCOMPARE(m.sample(100500, 500, true).playedMs, 500);
        QCOMPARE(m.sample(101000, 1000, false).playedMs, 500); // pause/loading/mute/stall
        QCOMPARE(m.sample(102000, 2000, true).playedMs, 500);
        QCOMPARE(m.sample(102500, 2500, true).playedMs, 1000);
        m.breakInterval(); // seek has a semantic fence, including tiny seek jumps
        QCOMPARE(m.sample(102600, 2600, true).playedMs, 1000);
        QCOMPARE(m.sample(102850, 2850, true).playedMs, 1250);
        m.reset();
        QCOMPARE(m.progress().playedMs, 0);
    }
    void coarseClockGapsAndDiscontinuities()
    {
        ObservedPlaybackMeter m;
        m.sample(0, 0, true);
        m.sample(0, 250, true);
        m.sample(0, 500, true);
        QCOMPARE(m.sample(1000, 1000, true).playedMs, 1000);
        QCOMPARE(m.sample(2000, 5000, true).playedMs, 1000); // unobserved scheduler/suspend gap
        QCOMPARE(m.sample(2500, 5500, true).playedMs, 1500);
        QCOMPARE(m.sample(100000, 5600, true).playedMs, 1500); // forward jump
        QCOMPARE(m.sample(0, 5800, true).playedMs, 1500);      // backward jump
        QCOMPARE(m.sample(100, 5900, true).playedMs, 1600);
        QCOMPARE(m.sample(350, 6000, true).playedMs, 1700); // jitter never credits > elapsed
        QCOMPARE(m.sample(350, 8000, true).playedMs, 1700); // frozen cursor earns nothing
        QCOMPARE(m.sample(-1, 9000, true).playedMs, 1700);
        QCOMPARE(m.sample(0, std::numeric_limits<qint64>::max(), true).playedMs, 1700);
        QCOMPARE(m.sample(1, 0, true).playedMs, 1700); // reversed clock is not elapsed time
    }
    void meterCapsWithoutOverflow()
    {
        ObservedPlaybackMeter m;
        m.sample(0, 0, true);
        for (qint64 t = 2000; t <= ObservedPlayback::MaximumMs; t += 2000)
            m.sample(t, t, true);
        QCOMPARE(m.progress().playedMs, ObservedPlayback::MaximumMs);
        QVERIFY(m.progress().capped);
        m.breakInterval();
        m.sample(0, ObservedPlayback::MaximumMs + 1, true);
        QCOMPARE(m.sample(1000, ObservedPlayback::MaximumMs + 1001, true).playedMs,
                 ObservedPlayback::MaximumMs);
    }
    void historyRequiresEvidenceAndKeepsOriginalSource()
    {
        ListeningHistory h;
        const LoadToken t{{QUuid::createUuid(), 1}, 1};
        const auto entry = QUuid::createUuid();
        h.begin(t, "101", {"1"}, ListeningMode::Queue, entry);
        auto s = snapshot(t, 0);
        s.observed.reset();
        QVERIFY(!h.observe(s));
        QVERIFY(h.records().isEmpty()); // position/duration never count
        s.observed = ObservedPlayback{1000};
        QVERIFY(h.observe(s));
        QCOMPARE(h.records().size(), 1);
        QCOMPARE(h.records().first().playedMs, 1000);
        QCOMPARE(h.records().first().accountId, "101");
        QCOMPARE(h.records().first().queueEntry, entry);
        QCOMPARE(h.records().first().preview->startMs, 100000);
        QVERIFY(!h.observe(s)); // repeated delivery cannot add the same total twice
        s.observed = ObservedPlayback{900};
        QVERIFY(!h.observe(s));
        s.observed = ObservedPlayback{-1};
        QVERIFY(!h.observe(s));
        s.token.generation++;
        s.observed = ObservedPlayback{5000};
        QVERIFY(!h.observe(s));
        s = snapshot(t, 1200, PlaybackState::Ended);
        QVERIFY(h.observe(s));
        QCOMPARE(h.records().first().end, ListeningEnd::Ended);
        QCOMPARE(h.records().first().playedMs, 1200);
        QVERIFY(!h.observe(snapshot(t, 9000)));
        auto next = t;
        ++next.generation;
        h.begin(next, "101", {"1"}, ListeningMode::PersonalFm, {});
        QVERIFY(h.observe(snapshot(next, 700)));
        QCOMPARE(h.records().size(), 2); // same song, separate loads; no fake unique coverage
        QCOMPARE(h.records().first().source, ListeningMode::PersonalFm);
        QVERIFY(h.records().first().queueEntry.isNull());
    }
    void clearCannotResurrectQueuedTotals()
    {
        ListeningHistory h;
        LoadToken t{{QUuid::createUuid(), 1}, 1};
        h.begin(t, {}, {"1"}, ListeningMode::Queue, {});
        h.observe(snapshot(t, 1000));
        h.clear();
        QVERIFY(h.records().isEmpty());
        h.begin(t, {}, {"1"}, ListeningMode::Queue, {}); // repeated begin cannot resurrect a load
        QVERIFY(!h.observe(snapshot(t, 2000)));          // could have been sampled before clear
        ++t.generation;
        h.begin(t, {}, {"1"}, ListeningMode::Queue, {});
        QVERIFY(h.observe(snapshot(t, 300)));
        QCOMPARE(h.records().first().playedMs, 300);
    }
    void boundsAndAccountReset()
    {
        ListeningHistory h;
        LoadToken t{{QUuid::createUuid(), 1}, 1};
        for (int i = 1; i <= 300; ++i) {
            t.generation = i;
            h.begin(t, "101", {"1"}, ListeningMode::Queue, {});
            h.observe(snapshot(t, i));
        }
        QCOMPARE(h.records().size(), ListeningHistory::MaximumRecords);
        QCOMPARE(h.records().last().token.generation, 45);
        QCOMPARE(h.records().first().playedMs, 300);
        ++t.session.epoch;
        ++t.generation;
        h.begin(t, "202", {"1"}, ListeningMode::PersonalFm, {});
        QVERIFY(h.records().isEmpty());
        h.observe(snapshot(t, 100));
        QCOMPARE(h.records().first().accountId, "202");
        h.reset();
        QVERIFY(h.records().isEmpty());
        QVERIFY(!h.context().isValid());
    }
    void coordinatorOnlyAcceptsMonotonicEvidence()
    {
        SessionCoordinator session;
        FakeStreamResolver resolver;
        FakePlaybackBackend backend;
        PlaybackCoordinator p(session, resolver, backend);
        LoadToken t{session.snapshot().token, 1};
        QVERIFY(p.start(t, {{"1"}}));
        StreamSource source;
        source.song = {"1"};
        source.validUntil = QDeadlineTimer(60000);
        resolver.finish(0, source);
        PlaybackEvent event{t, PlaybackEvent::Kind::Observation};
        event.observed = ObservedPlayback{1000};
        emit backend.event(event);
        QVERIFY(!p.snapshot().observed); // not ready
        emit backend.event({t, PlaybackEvent::Kind::Ready});
        emit backend.event({t, PlaybackEvent::Kind::Position, {}, 100000});
        QVERIFY(!p.snapshot().observed);
        emit backend.event(event);
        QCOMPARE(p.snapshot().observed->playedMs, 1000);
        event.observed = ObservedPlayback{500};
        emit backend.event(event);
        QCOMPARE(p.snapshot().observed->playedMs, 1000);
        event.observed = ObservedPlayback{ObservedPlayback::MaximumMs + 1};
        emit backend.event(event);
        QCOMPARE(p.snapshot().observed->playedMs, 1000);
        emit backend.event({t, PlaybackEvent::Kind::Ended});
        QCOMPARE(p.snapshot().observed->playedMs, 1000);
        event.observed = ObservedPlayback{2000};
        emit backend.event(event);
        QCOMPARE(p.snapshot().observed->playedMs, 1000);
        ++t.generation;
        QVERIFY(p.start(t, {{"1"}}));
        QVERIFY(!p.snapshot().observed);
        emit backend.event(event);
        QVERIFY(!p.snapshot().observed);
    }
    void listeningRecordsQueueFmAndStop()
    {
        Harness h;
        QVERIFY(h.listening.replaceQueue({{"1"}, {"2"}}));
        const auto entry = h.listening.queue().active;
        h.progress(1000);
        QCOMPARE(h.listening.history().first().queueEntry, entry);
        QVERIFY(h.listening.next());
        QCOMPARE(h.listening.history().first().end, ListeningEnd::Replaced);
        h.progress(500);
        QVERIFY(h.listening.startFm());
        QCOMPARE(h.listening.history().size(), 2);
        h.batch();
        QTRY_VERIFY(!h.listening.waitingForFm());
        h.progress(750);
        QCOMPARE(h.listening.history().first().source, ListeningMode::PersonalFm);
        QVERIFY(h.listening.history().first().queueEntry.isNull());
        QVERIFY(h.listening.stop());
        QCOMPARE(h.listening.history().first().end, ListeningEnd::Stopped);
        QCOMPARE(h.listening.history().size(), 3);
        h.listening.clearHistory();
        QVERIFY(h.listening.history().isEmpty());
    }
    void rejectedCommandsDoNotRetireHistory()
    {
        Harness h;
        QVERIFY(h.listening.replaceQueue({{"1"}, {"2"}}));
        h.progress(1000);
        for (int i = 0; i < 32; ++i) {
            if (!h.f.poll())
                break;
        }
        QVERIFY(!h.listening.next());
        QVERIFY(!h.listening.stop());
        QVERIFY(!h.listening.startFm());
        QCOMPARE(h.listening.history().size(), 1);
        QCOMPARE(h.listening.history().first().end, ListeningEnd::Active);
        QCOMPARE(h.listening.history().first().playedMs, 1000);
    }
    void privacyAndReentrancy()
    {
        Harness h;
        QVERIFY(h.listening.replaceQueue({{"1"}}));
        h.progress(1000);
        const auto old = h.f.playback();
        bool once = true;
        const auto connection = connect(&h.listening, &ListeningSession::historyChanged, &h.f, [&] {
            if (once) {
                once = false;
                QVERIFY(h.listening.replaceQueue({{"9"}}));
            }
        });
        h.progress(1500, PlaybackState::Ended);
        QCOMPARE(h.listening.playback().song.value, "9");
        QCOMPARE(h.listening.history().first().end, ListeningEnd::Ended);
        disconnect(connection);
        h.progress(300);
        QVERIFY(h.f.signOut());
        QVERIFY(h.listening.history().isEmpty()); // getter barrier before deferred invalidation
        h.f.deliverPlayback({0, old});
        QCoreApplication::processEvents();
        QVERIFY(h.listening.history().isEmpty());
    }
    void explicitClearAndExternalOwnership()
    {
        Harness h;
        QVERIFY(h.listening.replaceQueue({{"1"}}));
        h.progress(1000);
        h.listening.clearHistory();
        h.progress(2000);
        QVERIFY(h.listening.history().isEmpty());
        QVERIFY(h.listening.replaceQueue({{"1"}}));
        h.progress(500);
        QVERIFY(h.f.playSong({"9"}));
        QCOMPARE(h.listening.history().first().end, ListeningEnd::Detached);
        h.progress(4000);
        QCOMPARE(h.listening.history().size(), 1);
        QCOMPARE(h.listening.history().first().playedMs, 500);
        h.f.close();
        QVERIFY(h.listening.history().isEmpty());
    }
};
QTEST_GUILESS_MAIN(ListeningHistoryTest)
#include "listening_history_test.moc"
