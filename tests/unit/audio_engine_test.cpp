#include "../support/fake_range_http.h"
#include "infrastructure/media/audio_engine.h"
#include "infrastructure/media/audio_store.h"
#include <QAudioDevice>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QMediaDevices>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace cirrus;
using namespace domain;
using application::PlaybackEvent;
using Kind = PlaybackEvent::Kind;
using media::AudioEngine;
using media::AudioStore;
namespace
{
QByteArray fixture(const QString& name)
{
    QFile file(QStringLiteral(AUDIO_FIXTURES "/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
MediaSource source(const QByteArray& body, const QString& format)
{
    MediaSource s{QUrl(QStringLiteral("https://m801.music.126.net/tone")), "standard", format,
                  body.size(), QDeadlineTimer(60000)};
    s.md5 = QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex();
    return s;
}
LoadToken token(quint64 n) { return LoadToken{{QUuid::createUuid(), 1}, n}; }
QList<Kind> kinds(const QList<PlaybackEvent>& events)
{
    QList<Kind> list;
    for (const auto& e : events)
        list.append(e.kind);
    return list;
}
} // namespace

// Plays on the real default output at volume 0; skipped without one.
class AudioEngineTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;
    test::RangeServer m_server;
    std::unique_ptr<AudioStore> m_store;
    std::unique_ptr<AudioEngine> m_engine;
    QList<PlaybackEvent> m_events;

    void load(const QString& file, const QString& format, LoadToken t)
    {
        m_server.body = fixture(file);
        m_engine->load(source(m_server.body, format), t);
    }
    bool has(Kind kind) const
    {
        return std::any_of(m_events.begin(), m_events.end(),
                           [kind](const PlaybackEvent& e) { return e.kind == kind; });
    }

  private slots:
    void init()
    {
        if (QMediaDevices::defaultAudioOutput().isNull())
            QSKIP("no audio output device");
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() +
                                                QStringLiteral("/audio-engine-XXXXXX"));
        AudioStore::Options options;
        options.root = m_dir->path() + QStringLiteral("/audio");
        m_store =
            std::make_unique<AudioStore>(options, std::make_unique<test::RangeManager>(m_server));
        m_store->setPartition(QStringLiteral("test"));
        m_engine = std::make_unique<AudioEngine>(*m_store);
        m_engine->setVolume(0);
        m_events.clear();
        connect(m_engine.get(), &AudioEngine::event, this,
                [this](PlaybackEvent e) { m_events.append(e); });
    }
    void cleanup()
    {
        m_engine.reset();
        m_store.reset();
        m_dir.reset();
    }

    void opensPlaysAndEnds()
    {
        const auto t = token(1);
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), t);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        // The old backend's order: Duration, Seekable, Volume, then Ready.
        QCOMPARE(kinds(m_events),
                 (QList<Kind>{Kind::Duration, Kind::Seekable, Kind::Volume, Kind::Ready}));
        QVERIFY(std::abs(m_events[0].value - 500) <= 30);
        QCOMPARE(m_events[1].value, 1);
        QCOMPARE(m_events[2].volumeLinear, std::optional(0.f));
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 5000);
        qint64 last = -1;
        bool playing = false;
        for (const auto& e : std::as_const(m_events)) {
            QCOMPARE(e.token, t);
            if (e.kind == Kind::State && e.state == PlaybackState::Playing)
                playing = true;
            if (e.kind == Kind::Position) {
                QVERIFY(e.value > last); // forwards only
                QVERIFY(e.value <= 500);
                last = e.value;
            }
        }
        QVERIFY(playing);
        QVERIFY(last > 0);
    }

    void seekIsConfirmedAtTheTarget()
    {
        load(QStringLiteral("tone.mp3"), QStringLiteral("mp3"), token(2));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->pause();
        m_engine->seek(300);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::SeekConfirmed), 5000);
        const auto confirmed =
            std::find_if(m_events.begin(), m_events.end(),
                         [](const PlaybackEvent& e) { return e.kind == Kind::SeekConfirmed; });
        QCOMPARE(confirmed->value, 300);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 5000);
    }

    void stopRetiresEverything()
    {
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), token(3));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        m_engine->stop();
        const auto count = m_events.size();
        QTest::qWait(400);
        QCOMPARE(m_events.size(), count); // no completion events, not even queued ones
    }

    void aWrongFormatFails()
    {
        load(QStringLiteral("tone.flac"), QStringLiteral("mp3"), token(4));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Failure), 5000);
        QCOMPARE(m_events.last().failure.error, PlaybackError::UnsupportedFormat);
        QVERIFY(!has(Kind::Ready));
    }

    void aNewLoadReplacesTheOld()
    {
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), token(5));
        const auto second = token(6);
        load(QStringLiteral("tone.mp3"), QStringLiteral("mp3"), second);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        QTest::qWait(100);
        for (const auto& e : std::as_const(m_events))
            QCOMPARE(e.token, second);
    }
};
QTEST_GUILESS_MAIN(AudioEngineTest)
#include "audio_engine_test.moc"
