#include "application/backend/backend_facade.h"

#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;

class BackendFacadeTest : public QObject
{
    Q_OBJECT
  private slots:
    void rejectsBeforeReadyAndBoundsMailbox();
    void newerIntentDropsAlreadyQueuedEvents();
    void closingRejectsLateStartupAndAcknowledgesPending();
    void readyObserverMayClose();
    void musicResultCannotSurviveLogout();
    void playbackHasIntentAndSessionBarriers();
};

static void ready(BackendFacade& facade)
{
    facade.start();
    BackendEvent event;
    event.kind = BackendEvent::Kind::Started;
    facade.deliver(event);
}

void BackendFacadeTest::rejectsBeforeReadyAndBoundsMailbox()
{
    BackendFacade facade;
    QCOMPARE(facade.beginLogin(), 0ULL);
    ready(facade);
    QSignalSpy commands(&facade, &BackendFacade::commandRequested);
    for (int i = 0; i < 32; ++i)
        QVERIFY(facade.poll() != 0);
    QCOMPARE(facade.poll(), 0ULL);
    BackendEvent ack;
    ack.kind = BackendEvent::Kind::Acknowledged;
    ack.commandId = qvariant_cast<BackendCommand>(commands.first().at(0)).id;
    ack.accepted = true;
    facade.deliver(ack);
    QVERIFY(facade.poll() != 0);
}

void BackendFacadeTest::newerIntentDropsAlreadyQueuedEvents()
{
    BackendFacade facade;
    ready(facade);
    QSignalSpy commands(&facade, &BackendFacade::commandRequested);
    QSignalSpy signedIn(&facade, &BackendFacade::signedIn);
    QVERIFY(facade.beginLogin() != 0);
    const auto login = qvariant_cast<BackendCommand>(commands.last().at(0));
    BackendEvent session;
    session.kind = BackendEvent::Kind::Session;
    session.generation = login.generation;
    session.session = {
        {QUuid::createUuid(), 3}, SessionPhase::Authenticated, QStringLiteral("101")};
    facade.deliver(session);
    QCOMPARE(facade.snapshot().phase, SessionPhase::Authenticated);
    QVERIFY(facade.signOut() != 0);
    QVERIFY(!facade.snapshot().token.isValid()); // intent barrier precedes backend work
    facade.deliver(session);                     // already queued before UI clicked logout
    session.kind = BackendEvent::Kind::SignedIn;
    facade.deliver(session);
    QCOMPARE(signedIn.size(), 0);
    QVERIFY(!facade.snapshot().token.isValid());
    QSignalSpy saved(&facade, &BackendFacade::savedSessionCompleted);
    session.kind = BackendEvent::Kind::Saved;
    session.saved = AuthFailure{AuthError::CredentialRejected};
    facade.deliver(session);
    QCOMPARE(saved.size(), 1); // old persistence outcome is reportable, never a login
    QVERIFY(!facade.snapshot().token.isValid());
}

void BackendFacadeTest::closingRejectsLateStartupAndAcknowledgesPending()
{
    BackendFacade facade;
    ready(facade);
    QSignalSpy ack(&facade, &BackendFacade::acknowledged);
    QSignalSpy stopped(&facade, &BackendFacade::stopped);
    QVERIFY(facade.beginLogin() != 0);
    facade.close();
    facade.close();
    QCOMPARE(ack.size(), 1);
    QCOMPARE(ack.first().at(1).toBool(), false);
    QCOMPARE(facade.restore(), 0ULL);
    BackendEvent late;
    late.kind = BackendEvent::Kind::Started;
    facade.deliver(late);
    QCOMPARE(facade.state(), BackendState::Stopping);
    facade.finishClose(true);
    facade.finishClose(false);
    QCOMPARE(stopped.size(), 1);
    QCOMPARE(stopped.first().first().toBool(), true);
    facade.start();
    QCOMPARE(facade.state(), BackendState::Stopped);
}

void BackendFacadeTest::readyObserverMayClose()
{
    BackendFacade facade;
    QSignalSpy started(&facade, &BackendFacade::started);
    connect(&facade, &BackendFacade::stateChanged, &facade, [&](BackendState state) {
        if (state == BackendState::Ready)
            facade.close();
    });
    ready(facade);
    QCOMPARE(facade.state(), BackendState::Stopping);
    QCOMPARE(started.size(), 0);
}

void BackendFacadeTest::musicResultCannotSurviveLogout()
{
    BackendFacade facade;
    ready(facade);
    BackendEvent event;
    event.kind = BackendEvent::Kind::Session;
    event.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
    facade.deliver(event);
    QSignalSpy requests(&facade, &BackendFacade::musicRequested);
    QSignalSpy completed(&facade, &BackendFacade::musicCompleted);
    const auto id = facade.lyrics({QStringLiteral("101")});
    QVERIFY(id != 0);
    const auto request = qvariant_cast<MusicCommand>(requests.first().first());
    QVERIFY(facade.signOut() != 0);
    facade.deliverMusic({id, request.generation, request.context, SongLyrics{}});
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(completed.first().at(2))).error,
             MusicError::StaleContext);
    QCOMPARE(facade.lyrics({QStringLiteral("101")}), 0ULL);
}

void BackendFacadeTest::playbackHasIntentAndSessionBarriers()
{
    BackendFacade facade;
    ready(facade);
    BackendEvent session;
    session.kind = BackendEvent::Kind::Session;
    session.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
    facade.deliver(session);
    QSignalSpy commands(&facade, &BackendFacade::playbackRequested);
    QVERIFY(facade.playSong({"1"}));
    const auto first = qvariant_cast<PlaybackCommand>(commands.last().first());
    QVERIFY(facade.playSong({"2"}));
    const auto second = qvariant_cast<PlaybackCommand>(commands.last().first());
    PlaybackSnapshot old;
    old.token = first.token;
    old.song = {"1"};
    old.state = PlaybackState::Playing;
    facade.deliverPlayback({first.generation, old});
    QCOMPARE(facade.playback().song.value, "2");
    PlaybackSnapshot current;
    current.token = second.token;
    current.song = {"2"};
    current.state = PlaybackState::Playing;
    facade.deliverPlayback({second.generation, current});
    QCOMPARE(facade.playback().state, PlaybackState::Playing);
    QVERIFY(facade.stopPlayback());
    facade.deliverPlayback({second.generation, current});
    QCOMPARE(facade.playback().state, PlaybackState::Empty);
    QVERIFY(facade.signOut());
    facade.deliverPlayback({second.generation, current});
    QCOMPARE(facade.playback().state, PlaybackState::Empty);
    QCOMPARE(facade.playSong({"3"}), 0ULL);
    facade.close();
    facade.deliverPlayback({second.generation, current});
    QCOMPARE(facade.playback().state, PlaybackState::Empty);
}

QTEST_GUILESS_MAIN(BackendFacadeTest)
#include "backend_facade_test.moc"
