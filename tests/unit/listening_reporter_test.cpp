#include "application/backend/backend_facade.h"
#include "application/playback/listening_reporter.h"
#include "application/playback/listening_session.h"
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus;
using namespace application;
using namespace domain;
namespace
{
// An offline facade (no worker): playback commands are acknowledged, report commands
// collected; the test plays the backend's part with deliverPlayback.
struct Harness {
    BackendFacade facade;
    ListeningSession session{facade};
    ListeningReporter reporter{facade, session};
    QList<PlaybackCommand> commands;
    QList<ListeningReportCommand> reports;
    quint64 positionRevision = 0;
    explicit Harness(SessionPhase phase = SessionPhase::Authenticated)
    {
        QObject::connect(&facade, &BackendFacade::playbackRequested, &facade,
                         [this](PlaybackCommand c) {
                             commands.append(c);
                             QTimer::singleShot(0, &facade, [this, c] {
                                 BackendEvent e;
                                 e.kind = BackendEvent::Kind::Acknowledged;
                                 e.commandId = c.id;
                                 e.accepted = true;
                                 facade.deliver(e);
                             });
                         });
        QObject::connect(&facade, &BackendFacade::listeningReportRequested, &facade,
                         [this](ListeningReportCommand c) { reports.append(c); });
        facade.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        facade.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1},
                     phase,
                     phase == SessionPhase::Authenticated ? QString("101") : QString()};
        facade.deliver(e);
    }
    PlaybackCommand play() const
    {
        for (auto it = commands.crbegin(); it != commands.crend(); ++it)
            if (it->action == PlaybackAction::Play)
                return *it;
        return {};
    }
    // The backend's snapshot of the current load: `state`, with `playedMs` observed.
    void state(PlaybackState state, qint64 playedMs)
    {
        const auto c = play();
        PlaybackSnapshot p;
        p.token = c.token;
        p.song = c.query.song;
        p.state = state;
        p.intent = c.intent;
        p.positionRevision = ++positionRevision;
        p.durationMs = 200000;
        p.seekable = true;
        p.observed = ObservedPlayback{playedMs};
        facade.deliverPlayback({c.generation, p});
    }
    void answer(int index)
    {
        ListeningReportDelivery d{reports[index].id,
                                  reports[index].generation,
                                  reports[index].context,
                                  {reports[index].report.eventId, ListeningReportAction::Play,
                                   ReportOutcome::Acknowledged, ReportError::None}};
        facade.deliverListeningReport(d);
    }
};
} // namespace

class ListeningReporterTest : public QObject
{
    Q_OBJECT
  private slots:
    void reportsOnceWhenTheUserGoesOn()
    {
        Harness h;
        QHash<QUuid, ListeningReporter::Origin> origins;
        h.reporter.setOriginLookup([&](QUuid entry) { return origins.value(entry); });
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        const auto queue = h.session.queue();
        origins.insert(queue.entries[0].id, {ListeningReportSource::Album, "77"});
        h.state(PlaybackState::Playing, 12400);
        QVERIFY(h.reports.isEmpty()); // still playing
        QVERIFY(h.session.next());
        QCOMPARE(h.reports.size(), 1);
        const auto r = h.reports.first().report;
        QCOMPARE(r.song.value, "1");
        QCOMPARE(r.playedSeconds, 12);
        QCOMPARE(r.source, ListeningReportSource::Album);
        QCOMPARE(r.sourceId, "77");
        QCOMPARE(r.end, ListeningReportEnd::Ui);
        QCOMPARE(h.reports.first().accountId, "101");
    }
    void endedFailedAndShortLoads()
    {
        Harness h;
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}, {"3"}}));
        h.state(PlaybackState::Playing, 2900);
        QVERIFY(h.session.next()); // under 3 s: not reported
        QVERIFY(h.reports.isEmpty());
        h.state(PlaybackState::Playing, 200000);
        h.state(PlaybackState::Ended, 200000);
        QCOMPARE(h.reports.size(), 1);
        QCOMPARE(h.reports.last().report.end, ListeningReportEnd::PlayEnd);
        QCOMPARE(h.reports.last().report.song.value, "2");
        // No origin known: the song itself.
        QCOMPARE(h.reports.last().report.source, ListeningReportSource::Song);
        QCOMPARE(h.reports.last().report.sourceId, "2");
        QTRY_COMPARE(h.play().query.song.value, "3"); // advanced
        h.state(PlaybackState::Playing, 5000);
        h.state(PlaybackState::Failed, 5000);
        QCOMPARE(h.reports.size(), 2);
        QCOMPARE(h.reports.last().report.end, ListeningReportEnd::Exception);
    }
    void longPauseSettlesOnce()
    {
        Harness h;
        h.reporter.setPauseTimeout(50);
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        h.state(PlaybackState::Playing, 8000);
        h.state(PlaybackState::Paused, 8000);
        QTRY_COMPARE(h.reports.size(), 1);
        QCOMPARE(h.reports.first().report.playedSeconds, 8);
        QCOMPARE(h.reports.first().report.end, ListeningReportEnd::Interrupt);
        // Playing on after that never reports this load again.
        h.state(PlaybackState::Playing, 20000);
        QVERIFY(h.session.next());
        QCOMPARE(h.reports.size(), 1);
        // A short pause is not a settlement.
        h.state(PlaybackState::Playing, 6000);
        h.state(PlaybackState::Paused, 6000);
        h.state(PlaybackState::Playing, 6000);
        QTest::qWait(120);
        QCOMPARE(h.reports.size(), 1);
    }
    void guestsAndFmAreNotReported()
    {
        Harness guest(SessionPhase::Guest);
        QVERIFY(guest.session.replaceQueue({{"1"}, {"2"}}));
        guest.state(PlaybackState::Playing, 60000);
        QVERIFY(guest.session.next());
        QVERIFY(guest.reports.isEmpty());
    }
    void flushSettlesThePlayingLoadAndWaits()
    {
        Harness h;
        QVERIFY(h.session.replaceQueue({{"1"}}));
        h.state(PlaybackState::Playing, 30000);
        bool done = false;
        h.reporter.flush([&] { done = true; }, 5000);
        QCOMPARE(h.reports.size(), 1);
        QCOMPARE(h.reports.first().report.end, ListeningReportEnd::Interrupt);
        QTest::qWait(50);
        QVERIFY(!done); // waits for the answer
        h.answer(0);
        QVERIFY(done);
        // Without an answer it gives up after the timeout.
        Harness late;
        QVERIFY(late.session.replaceQueue({{"1"}}));
        late.state(PlaybackState::Playing, 30000);
        bool lateDone = false;
        late.reporter.flush([&] { lateDone = true; }, 50);
        QTRY_VERIFY(lateDone);
        // Nothing to settle: done straight away (but not inside the call).
        Harness idle;
        bool idleDone = false;
        idle.reporter.flush([&] { idleDone = true; });
        QVERIFY(!idleDone);
        QTRY_VERIFY(idleDone);
    }
    void searchKeywordsAreTheSourceId()
    {
        Harness h;
        h.reporter.setOriginLookup([](QUuid) {
            return std::optional(
                ListeningReporter::Origin{ListeningReportSource::Search, "富士山下"});
        });
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        h.state(PlaybackState::Playing, 4000);
        QVERIFY(h.session.next());
        QCOMPARE(h.reports.first().report.source, ListeningReportSource::Search);
        QCOMPARE(h.reports.first().report.sourceId, "富士山下");
        // An origin that isn't valid falls back to the song.
        h.reporter.setOriginLookup([](QUuid) {
            return std::optional(ListeningReporter::Origin{ListeningReportSource::Album, ""});
        });
        h.state(PlaybackState::Playing, 4000);
        h.state(PlaybackState::Ended, 4000);
        QCOMPARE(h.reports.last().report.source, ListeningReportSource::Song);
    }
};
QTEST_GUILESS_MAIN(ListeningReporterTest)
#include "listening_reporter_test.moc"
