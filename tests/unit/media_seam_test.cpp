#include "../support/fake_playback.h"
#include "application/playback/playback_coordinator.h"
#include "domain/playback/playback_entity.h"
#include <QSignalSpy>
#include <QtTest>
#include <type_traits>

using namespace cirrus;
using namespace cirrus::domain;
using namespace cirrus::application;

template <class T>
concept HasSong = requires(T value) { value.song; };
static_assert(!HasSong<MediaSource>);
static_assert(HasSong<StreamSource>);
static_assert(std::is_same_v<decltype(&IPlaybackBackend::load),
                             void (IPlaybackBackend::*)(MediaSource, LoadToken)>);
static_assert(!std::is_convertible_v<MediaSource, StreamSource>);
static_assert(!std::is_convertible_v<ProgramId, SongId>);
static_assert(!std::is_convertible_v<VoiceId, ProgramId>);
static_assert(!std::is_convertible_v<AudioTrackId, SongId>);
static_assert(!std::is_convertible_v<SongId, AudioTrackId>);

namespace
{
StreamSource songSource(QString id)
{
    return {{std::move(id)},
            QUrl("https://m802.music.126.net/synthetic-seam.mp3"),
            "standard",
            "mp3",
            8,
            QDeadlineTimer(60000),
            PreviewRange{1000, 3000}};
}
class CapturingBackend final : public IPlaybackBackend
{
  public:
    QList<MediaSource> payloads;
    QList<LoadToken> loads;
    int plays = 0, stops = 0;
    void load(MediaSource source, LoadToken token) override
    {
        payloads.append(std::move(source));
        loads.append(token);
    }
    void play() override { ++plays; }
    void pause() override {}
    void stop() override { ++stops; }
    void seek(qint64) override {}
    void setVolume(float) override {}
};
} // namespace
class MediaSeamTest final : public QObject
{
    Q_OBJECT
  private slots:
    void canonicalIds_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("valid");
        const QStringList bad{"",
                              "0",
                              "-1",
                              "+1",
                              "01",
                              " 1",
                              "1 ",
                              "1.0",
                              QString(QChar(0)),
                              QString(QChar(0xff11)),
                              "9223372036854775808"};
        for (int i = 0; i < bad.size(); ++i)
            QTest::newRow(qPrintable("bad-" + QString::number(i))) << bad[i] << false;
        for (const auto& value : QStringList{"1", "9007199254740993", "9223372036854775807"})
            QTest::newRow(qPrintable(value)) << value << true;
    }
    void canonicalIds()
    {
        QFETCH(QString, value);
        QFETCH(bool, valid);
        QCOMPARE(AudioTrackId{value}.isValid(), valid);
        QCOMPARE(validPlaybackEntity(SongId{value}), valid);
        QCOMPARE(validPlaybackEntity(ProgramId{value}), valid);
        QCOMPARE(validPlaybackEntity(VoiceId{value}), valid);
    }
    void namespaces_data()
    {
        QTest::addColumn<int>("left");
        QTest::addColumn<int>("right");
        for (int l = 0; l < 3; ++l)
            for (int r = 0; r < 3; ++r)
                QTest::newRow(qPrintable(QString::number(l) + "-" + QString::number(r))) << l << r;
    }
    void namespaces()
    {
        QFETCH(int, left);
        QFETCH(int, right);
        const QList<PlaybackEntity> same{SongId{"9007199254740993"}, ProgramId{"9007199254740993"},
                                         VoiceId{"9007199254740993"}};
        const QList<PlaybackEntity> different{SongId{"101"}, ProgramId{"102"}, VoiceId{"103"}};
        QCOMPARE(samePlaybackEntity(same[left], same[right]), left == right);
        QVERIFY(!samePlaybackEntity(same[left], different[right]));
        QVERIFY(!samePlaybackEntity(different[right], same[left]));
    }
    void physicalPayloadDoesNotCarryIdentity()
    {
        auto source = songSource("101");
        const auto deadline = source.validUntil;
        MediaSource physical = std::move(source);
        QCOMPARE(physical.url, QUrl("https://m802.music.126.net/synthetic-seam.mp3"));
        QCOMPARE(physical.actualQuality, "standard");
        QCOMPARE(physical.format, "mp3");
        QCOMPARE(physical.size, 8);
        QVERIFY(physical.validUntil == deadline);
        QVERIFY(physical.preview);
        QCOMPARE(physical.preview->startMs, 1000);
        QCOMPARE(physical.preview->endMs, 3000);
        // A raw payload is NOT a provider binding or a permission proof.
        MediaSource untrusted;
        untrusted.url = QUrl("file:///synthetic");
        untrusted.validUntil = QDeadlineTimer(QDeadlineTimer::Forever);
        QVERIFY(untrusted.validUntil.isForever()); // existing media policy must still reject it
    }
    void songOwnerKeepsIdentityAndRetiresOldLoads()
    {
        SessionCoordinator session;
        test::FakeStreamResolver resolver;
        CapturingBackend backend;
        PlaybackCoordinator owner(session, resolver, backend);
        const auto context = session.snapshot().token;
        const LoadToken first{context, 1}, second{context, 2}, third{context, 3};
        QVERIFY(owner.start(first, {{"101"}}, TransportIntent::Pause));
        resolver.finish(0, songSource("101"));
        QCOMPARE(backend.payloads.size(), 1);
        QCOMPARE(owner.snapshot().song.value, "101");
        QCOMPARE(backend.loads.first(), first);
        emit backend.event({first, PlaybackEvent::Kind::Ready});
        QCOMPARE(owner.snapshot().state, PlaybackState::Paused);
        QCOMPARE(backend.plays, 0);
        QVERIFY(owner.start(second, {{"102"}}));
        QVERIFY(owner.start(third, {{"103"}}));
        resolver.finish(1, songSource("102"));
        QCOMPARE(backend.loads.size(), 1);
        resolver.finish(2, songSource("103"));
        QCOMPARE(backend.loads.size(), 2);
        QCOMPARE(backend.loads.last(), third);
        QCOMPARE(owner.snapshot().song.value, "103");
        QCOMPARE(backend.payloads.last().preview->startMs, 1000);
        QCOMPARE(backend.payloads.last().preview->endMs, 3000);
        const auto current = owner.snapshot().state;
        emit backend.event({first, PlaybackEvent::Kind::Ended});
        emit backend.event({second, PlaybackEvent::Kind::Failure, {}, 0, {PlaybackError::Decode}});
        QCOMPARE(owner.snapshot().state, current);
        emit backend.event({third, PlaybackEvent::Kind::Ready});
        QCOMPARE(backend.plays, 1);
        emit backend.event({third, PlaybackEvent::Kind::Ended});
        const auto retired = backend.stops;
        emit backend.event({third, PlaybackEvent::Kind::Ended});
        QCOMPARE(backend.stops, retired);
        QCOMPARE(owner.snapshot().state, PlaybackState::Ended);
        QVERIFY(!owner.start(third, {{"104"}}));
        QCOMPARE(resolver.requests.size(), 3);
    }
    void validationStaysBeforeMedia()
    {
        SessionCoordinator session;
        test::FakeStreamResolver resolver;
        CapturingBackend backend;
        PlaybackCoordinator owner(session, resolver, backend);
        const auto context = session.snapshot().token;
        QVERIFY(owner.start({context, 1}, {{"101"}}));
        resolver.finish(0, songSource("102"));
        QCOMPARE(owner.snapshot().failure->error, PlaybackError::InvalidResponse);
        QVERIFY(backend.payloads.isEmpty());
        QVERIFY(owner.start({context, 2}, {{"101"}}));
        auto expired = songSource("101");
        expired.validUntil = QDeadlineTimer(0);
        resolver.finish(1, expired);
        QCOMPARE(owner.snapshot().failure->error, PlaybackError::Expired);
        QVERIFY(backend.payloads.isEmpty());
        QVERIFY(owner.start({context, 3}, {{"101"}}));
        static_cast<void>(session.beginAuthentication());
        resolver.finish(2, songSource("101"));
        QVERIFY(backend.payloads.isEmpty());
        QCOMPARE(owner.snapshot().state, PlaybackState::Empty);
        QVERIFY(!owner.start({context, 4}, {{"101"}}));
        QCOMPARE(resolver.requests.size(), 3);
    }
};
QTEST_GUILESS_MAIN(MediaSeamTest)
#include "media_seam_test.moc"
