#include "application/music/personal_fm_source.h"
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::application;
using namespace cirrus::domain;
namespace
{
PersonalFmBatch batch(std::initializer_list<int> ids)
{
    PersonalFmBatch b;
    for (auto id : ids) {
        Song s;
        s.id = {QString::number(id)};
        b.songs.append(s);
    }
    return b;
}
struct Harness {
    BackendFacade f;
    PersonalFmSource source{f};
    QList<MusicCommand> commands;
    Harness(bool authenticated = true)
    {
        QObject::connect(&f, &BackendFacade::musicRequested, &f,
                         [this](MusicCommand c) { commands.append(c); });
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1},
                     authenticated ? SessionPhase::Authenticated : SessionPhase::Guest,
                     authenticated ? QString("1") : QString{}};
        f.deliver(e);
    }
    void deliver(MusicResult r)
    {
        const auto c = commands.last();
        f.deliverMusic({c.id, c.generation, c.context, std::move(r)});
    }
};
} // namespace
class PersonalFmTest : public QObject
{
    Q_OBJECT
  private slots:
    void guestAndDailyAuthGate()
    {
        Harness h(false);
        QVERIFY(!h.source.start());
        QVERIFY(!h.source.retry());
        QVERIFY(!h.source.takeNext());
        QCOMPARE(h.f.dailyRecommendations(), 0);
        QCOMPARE(h.f.personalFmBatch(), 0);
        QVERIFY(h.commands.isEmpty());
        QVERIFY(!h.source.snapshot().active);
        Harness account;
        QVERIFY(account.f.dailyRecommendations());
        QVERIFY(std::holds_alternative<DailyRecommendationsQuery>(account.commands.last().query));
    }
    void fifoDedupAndSingleFlight()
    {
        Harness h;
        QSignalSpy playback(&h.f, &BackendFacade::playbackRequested);
        QVERIFY(h.source.start());
        QVERIFY(!h.source.start());
        QVERIFY(!h.source.retry());
        QCOMPARE(h.commands.size(), 1);
        QVERIFY(h.source.snapshot().fetching);
        h.deliver(batch({1, 1, 2, 3}));
        QCOMPARE(h.source.snapshot().upcoming.size(), 3);
        QCOMPARE(h.source.takeNext()->id.value, "1");
        QCOMPARE(h.commands.size(), 2);
        QCOMPARE(h.source.takeNext()->id.value, "2");
        QCOMPARE(h.source.takeNext()->id.value, "3");
        QVERIFY(!h.source.takeNext());
        QCOMPARE(h.commands.size(), 2);
        h.deliver(batch({1, 4, 5}));
        QCOMPARE(h.source.snapshot().upcoming.size(), 2);
        QCOMPARE(h.source.takeNext()->id.value, "4");
        QCOMPARE(h.commands.size(), 3);
        h.source.stop();
        QVERIFY(!h.source.snapshot().active);
        QCOMPARE(playback.size(), 0); // supply is deliberately not an ordinary repeating player
    }
    void failedRefillPreservesItemsAndExplicitRetry()
    {
        Harness h;
        QVERIFY(h.source.start());
        h.deliver(batch({1, 2, 3}));
        QVERIFY(h.source.takeNext());
        h.deliver(MusicFailure{MusicError::Timeout});
        QCOMPARE(h.source.snapshot().upcoming.size(), 2);
        QCOMPARE(h.source.snapshot().failure->error, MusicError::Timeout);
        QVERIFY(h.source.takeNext());
        QVERIFY(h.source.takeNext());
        QVERIFY(!h.source.takeNext());
        QCOMPARE(h.commands.size(), 2);
        QVERIFY(h.source.retry());
        QVERIFY(!h.source.retry());
        h.deliver(batch({}));
        QVERIFY(h.source.snapshot().refillBlocked);
        QVERIFY(!h.source.snapshot().failure);
        for (int i = 0; i < 10; ++i) {
            QVERIFY(!h.source.takeNext());
            QCoreApplication::processEvents();
        }
        QCOMPARE(h.commands.size(), 3);
        QVERIFY(h.source.retry());
        h.deliver(batch({1, 2}));
        QVERIFY(h.source.snapshot().upcoming.isEmpty());
        QVERIFY(h.source.snapshot().refillBlocked);
        QCOMPARE(h.commands.size(), 4);
        QVERIFY(h.source.retry());
        h.deliver(batch({6}));
        QCOMPARE(h.source.snapshot().upcoming.first().id.value, "6");
        QVERIFY(!h.source.snapshot().refillBlocked);
    }
    void batchBoundsAndInvalidResults()
    {
        Harness h;
        QVERIFY(h.source.start());
        auto b = batch({1});
        for (int i = 2; i <= 21; ++i) {
            auto next = batch({i});
            b.songs.append(next.songs);
        }
        h.deliver(b);
        QVERIFY(h.source.snapshot().upcoming.isEmpty());
        QCOMPARE(h.source.snapshot().failure->error, MusicError::InvalidResponse);
        QVERIFY(h.source.retry());
        b = batch({1, 2});
        b.songs.last().id.value = "01";
        h.deliver(b);
        QVERIFY(h.source.snapshot().upcoming.isEmpty());
        QVERIFY(h.source.retry());
        h.deliver(DailyRecommendations{});
        QCOMPARE(h.source.snapshot().failure->error, MusicError::InvalidResponse);
        QVERIFY(h.source.retry());
        b.songs.clear();
        for (int i = 1; i <= 20; ++i)
            b.songs.append(batch({i}).songs);
        h.deliver(b);
        QCOMPARE(h.source.snapshot().upcoming.size(), 20);
        const auto before = h.commands.size();
        for (int i = 0; i < 18; ++i)
            QVERIFY(h.source.takeNext());
        QCOMPARE(h.commands.size(), before + 1);
        QVERIFY(!h.source.retry());
        b.songs.clear();
        for (int i = 21; i <= 40; ++i)
            b.songs.append(batch({i}).songs);
        h.deliver(b);
        QCOMPARE(h.source.snapshot().upcoming.size(), 22);
        QVERIFY(!h.source.retry());
    }
    void stopRestartAndLateBatch()
    {
        Harness h;
        QSignalSpy canceled(&h.f, &BackendFacade::musicCancelRequested);
        QVERIFY(h.source.start());
        const auto old = h.commands.last();
        h.source.stop();
        QCOMPARE(canceled.size(), 1);
        QVERIFY(h.source.start());
        h.f.deliverMusic({old.id, old.generation, old.context, batch({1})});
        QVERIFY(h.source.snapshot().upcoming.isEmpty());
        QVERIFY(h.source.snapshot().fetching);
        h.deliver(batch({2}));
        QCOMPARE(h.source.snapshot().upcoming.first().id.value, "2");
        h.f.close();
        QVERIFY(!h.source.snapshot().active);
        QVERIFY(!h.source.takeNext());
    }
    void logoutPrivacyAndNewAccount()
    {
        Harness h;
        QVERIFY(h.source.start());
        h.deliver(batch({1, 2, 3}));
        QVERIFY(h.source.takeNext());
        const auto old = h.commands.last();
        QVERIFY(h.f.signOut()); // getters are hidden before the deferred session notification
        QVERIFY(h.source.snapshot().upcoming.isEmpty());
        QVERIFY(!h.source.takeNext());
        QVERIFY(!h.source.retry());
        h.f.deliverMusic({old.id, old.generation, old.context, batch({4})});
        QCoreApplication::processEvents();
        QVERIFY(!h.source.snapshot().active);
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.generation = 1;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Authenticated, "2"};
        h.f.deliver(e);
        QVERIFY(h.source.start());
        h.deliver(batch({1})); // recent IDs from account 1 were erased
        QCOMPARE(h.source.snapshot().upcoming.first().id.value, "1");
    }
    void fullMailboxIsBoundedFailure()
    {
        Harness h;
        for (int i = 0; i < 16; ++i)
            QVERIFY(h.f.searchSongs({"synthetic"}));
        QVERIFY(h.source.start());
        QCOMPARE(h.commands.size(), 16);
        QVERIFY(h.source.snapshot().active);
        QVERIFY(h.source.snapshot().refillBlocked);
        QCOMPARE(h.source.snapshot().failure->error, MusicError::Busy);
        h.f.cancelMusic(h.commands.first().id);
        QVERIFY(h.source.retry());
        QCOMPARE(h.commands.size(), 17);
        h.deliver(batch({1}));
        QVERIFY(!h.source.snapshot().failure);
    }
    void reentrantStopAndLogoutWin()
    {
        Harness h;
        bool intercept = true;
        const auto connection =
            connect(&h.f, &BackendFacade::musicRequested, &h.f, [&](MusicCommand) {
                if (intercept) {
                    intercept = false;
                    h.source.stop();
                    QVERIFY(h.source.start()); // nested new intent wins before old submit returns
                }
            });
        QVERIFY(h.source.start());
        QVERIFY(h.source.snapshot().active);
        QVERIFY(h.source.snapshot().fetching);
        QCOMPARE(h.commands.size(), 2);
        disconnect(connection);
        h.deliver(batch({1, 2, 3}));
        const auto logout =
            connect(&h.source, &PersonalFmSource::changed, &h.f, [&](PersonalFmSnapshot s) {
                if (s.active)
                    QVERIFY(h.f.signOut());
            });
        QVERIFY(!h.source.takeNext());
        QVERIFY(h.source.snapshot().upcoming.isEmpty());
        disconnect(logout);
    }
    void destructionCancelsPendingOnly()
    {
        BackendFacade f;
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Authenticated, "1"};
        f.deliver(e);
        QSignalSpy cancel(&f, &BackendFacade::musicCancelRequested),
            playback(&f, &BackendFacade::playbackRequested);
        {
            PersonalFmSource source(f);
            QVERIFY(source.start());
        }
        QCOMPARE(cancel.size(), 1);
        QCOMPARE(playback.size(), 0);
        QCoreApplication::processEvents();
    }
};
QTEST_GUILESS_MAIN(PersonalFmTest)
#include "personal_fm_test.moc"
