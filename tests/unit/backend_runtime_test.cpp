#include "../support/fake_http.h"
#include "../support/fake_secret_store.h"
#include "application/music/personal_fm_source.h"
#include "application/playback/listening_session.h"
#include "application/ports/playback.h"
#include "infrastructure/bootstrap/xeapi_key_vault.h"
#include "infrastructure/netease/network/http_transport.h"
#include "infrastructure/netease/protocol/openssl_primitives.h"
#include "infrastructure/runtime/backend_runtime.h"
#include "infrastructure/secrets/tracked_secret_store.h"
#include "infrastructure/storage/installation_store.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>

using namespace cirrus;
using namespace cirrus::application;
using namespace cirrus::runtime;
using namespace cirrus::secrets;
using namespace cirrus::test;

// All synthetic records stay in memory; only the non-secret installation DB is on disk.
class Gate : public QObject
{
    Q_OBJECT
  signals:
    void release();
    void endPlayback();
    void observedPlayback(qint64 playedMs);
};

struct SharedState {
    QMutex mutex;
    QHash<QString, QByteArray> records;
    std::atomic_int created{0}, destroyed{0}, wrongThread{0}, requests{0};
    std::atomic_int reads{0}, writes{0}, removes{0};
    std::atomic_bool holdReads{false}, holdWrites{false}, failRemove{false}, failWrite{false};
    bool contains(const QString& key)
    {
        QMutexLocker lock(&mutex);
        return records.contains(key);
    }
};

class ControlledStore final : public QObject, public SecretStore
{
  public:
    ControlledStore(std::shared_ptr<SharedState> state, Gate& gate) : m_state(std::move(state))
    {
        checkThread();
        ++m_state->created;
        connect(
            &gate, &Gate::release, this,
            [this] {
                const auto jobs = std::exchange(m_jobs, {});
                for (const auto& job : jobs)
                    job();
            },
            Qt::QueuedConnection);
    }
    ~ControlledStore() override
    {
        checkThread();
        ++m_state->destroyed;
    }
    void read(const QString& key, QObject* context, std::function<void(SecretResult)> done) override
    {
        ++m_state->reads;
        schedule(context, m_state->holdReads, [this, key, done] {
            SecretResult result;
            {
                QMutexLocker lock(&m_state->mutex);
                result.error =
                    m_state->records.contains(key) ? SecretError::None : SecretError::NotFound;
                result.data = m_state->records.value(key);
            }
            done(result);
        });
    }
    void write(const QString& key, const QByteArray& data, QObject* context,
               std::function<void(SecretError)> done) override
    {
        ++m_state->writes;
        schedule(context, m_state->holdWrites, [this, key, data, done] {
            if (m_state->failWrite) {
                done(SecretError::Unavailable);
                return;
            }
            {
                QMutexLocker lock(&m_state->mutex);
                m_state->records.insert(key, data);
            }
            done(SecretError::None);
        });
    }
    void remove(const QString& key, QObject* context,
                std::function<void(SecretError)> done) override
    {
        ++m_state->removes;
        schedule(context, false, [this, key, done] {
            if (m_state->failRemove) {
                done(SecretError::Denied);
                return;
            }
            {
                QMutexLocker lock(&m_state->mutex);
                m_state->records.remove(key);
            }
            done(SecretError::None);
        });
    }

  private:
    void checkThread()
    {
        if (QThread::currentThread() == qApp->thread())
            ++m_state->wrongThread;
    }
    void schedule(QObject* context, bool hold, std::function<void()> done)
    {
        checkThread();
        if (context->thread() != thread())
            ++m_state->wrongThread;
        auto job = [target = QPointer<QObject>(context), done] {
            if (target)
                done();
        };
        if (hold)
            m_jobs.append(job);
        else
            QTimer::singleShot(0, this, job);
    }
    std::shared_ptr<SharedState> m_state;
    QList<std::function<void()>> m_jobs;
};

class CountingManager final : public FakeManager
{
  public:
    explicit CountingManager(std::shared_ptr<SharedState> state) : m_state(std::move(state))
    {
        check();
        ++m_state->created;
    }
    ~CountingManager() override
    {
        check();
        ++m_state->destroyed;
    }

  protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoing) override
    {
        check();
        ++m_state->requests;
        return FakeManager::createRequest(op, request, outgoing);
    }

  private:
    void check()
    {
        if (QThread::currentThread() == qApp->thread())
            ++m_state->wrongThread;
    }
    std::shared_ptr<SharedState> m_state;
};

static BackendFactories factories(std::shared_ptr<SharedState> state, Gate& gate,
                                  QList<Plan> plans = {})
{
    BackendFactories f;
    f.secrets = [state, &gate] { return std::make_unique<ControlledStore>(state, gate); };
    f.apiTransport = [state, plans] {
        auto manager = std::make_unique<CountingManager>(state);
        manager->plans = plans;
        return std::make_unique<netease::HttpTransport>(std::move(manager));
    };
    f.keyTransport = [state] {
        auto manager = std::make_unique<CountingManager>(state);
        manager->plans = {Plan{"{}", 503}}; // never contacts a real key registrar
        return std::make_unique<netease::HttpTransport>(std::move(manager));
    };
    return f;
}

static QString seed(const QString& path, SharedState& state)
{
    storage::DatabaseWorker db(path, storage::InstallationStore::migrations());
    QSignalSpy opened(&db, &storage::DatabaseWorker::opened);
    db.open();
    if (!opened.wait(3000) ||
        qvariant_cast<storage::StorageError>(opened.first().first()) != storage::StorageError::None)
        qFatal("synthetic installation setup failed");
    storage::InstallationStore installation(db);
    QString device;
    installation.deviceId(&db,
                          [&](storage::StorageResult<QString> result) { device = result.value; });
    if (!QTest::qWaitFor([&] { return !device.isEmpty(); }, 3000))
        qFatal("synthetic device setup failed");
    const netease::XeapiPublicKey key{
        QStringLiteral("v7"),
        QByteArray::fromBase64("eaYx7t4b+cmPEgMs3q3Q56B5OY/HhriMyEbsia+FpRo="), "sk-test-123"};
    state.records.insert(QStringLiteral("xeapi-key"), bootstrap::XeapiKeyVault::serialize(key));
    state.records.insert(
        QStringLiteral("anonymous-token"),
        QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                  {QStringLiteral("deviceId"), device},
                                  {QStringLiteral("token"), QStringLiteral("synthetic-anon")}})
            .toJson(QJsonDocument::Compact));
    return device;
}

static QList<Plan> loginPlans()
{
    Plan auth{R"({"code":803})"};
    auth.headers.append(std::pair<QByteArray, QByteArray>{
        "Set-Cookie",
        "MUSIC_U=synthetic-account; Domain=.music.163.com; Path=/; Secure; HttpOnly"});
    return {
        Plan{R"({"code":200,"unikey":"synthetic-key"})"}, auth,
        Plan{
            R"({"code":200,"account":{"id":101},"profile":{"userId":101,"nickname":"Synthetic"}})"}};
}

class BackendRuntimeTest : public QObject
{
    Q_OBJECT
  private slots:
    void startLoginCloseRestore();
    void musicQueriesThroughThread();
    void discoveryThroughThread();
    void libraryThroughThread();
    void listeningReportThroughThread();
    void cloudHistoryThroughThread();
    void chartsToPlaylistPlaybackThroughThread();
    void playlistDiscoveryThroughThread();
    void entitySearchThroughThread();
    void podcastReadsThroughThread();
    void commentsThroughThread();
    void accountLibraryThroughThread();
    void collectionsThroughThread();
    void playbackThroughThread_data();
    void playbackThroughThread();
    void closeDuringRead();
    void closeDuringAnonymousWrite();
    void anonymousPersistenceFailureIsReported();
    void closeDuringQrRequest();
    void closeWhileDatabaseOpening();
    void closeDrainsWrite_data();
    void closeDrainsWrite();
    void destructionStillDrains();
    void startupFailure_data();
    void startupFailure();
    void closeBeforeStart();
    void trackedStoreKeepsDeadRecipientsAccounted();
};

void BackendRuntimeTest::playbackThroughThread_data()
{
    QTest::addColumn<bool>("fm");
    QTest::newRow("queue") << false;
    QTest::newRow("fm") << true;
}
void BackendRuntimeTest::playbackThroughThread()
{
    QFETCH(bool, fm);
    class Player final : public IPlaybackBackend
    {
      public:
        std::shared_ptr<SharedState> state;
        domain::LoadToken token;
        explicit Player(std::shared_ptr<SharedState> s, Gate& gate) : state(std::move(s))
        {
            check();
            ++state->created;
            connect(
                &gate, &Gate::observedPlayback, this,
                [this](qint64 played) {
                    check();
                    PlaybackEvent observation{token, PlaybackEvent::Kind::Observation};
                    observation.observed = domain::ObservedPlayback{played};
                    emit event(observation);
                },
                Qt::QueuedConnection);
            connect(
                &gate, &Gate::endPlayback, this,
                [this] {
                    check();
                    emit event({token, PlaybackEvent::Kind::Ended});
                },
                Qt::QueuedConnection);
        }
        ~Player() override
        {
            check();
            ++state->destroyed;
        }
        void check()
        {
            if (QThread::currentThread() == qApp->thread())
                ++state->wrongThread;
        }
        void load(domain::StreamSource, domain::LoadToken t) override
        {
            check();
            token = t;
            QTimer::singleShot(0, this, [this, t] {
                emit event({t, PlaybackEvent::Kind::Duration, {}, 3000});
                emit event({t, PlaybackEvent::Kind::Seekable, {}, 1});
                emit event({t, PlaybackEvent::Kind::Ready});
            });
        }
        void play() override
        {
            check();
            emit event({token, PlaybackEvent::Kind::State, domain::PlaybackState::Playing});
        }
        void pause() override { check(); }
        void stop() override { check(); }
        void seek(qint64 position) override
        {
            check();
            emit event({token, PlaybackEvent::Kind::Position, {}, position});
        }
        void setVolume(float) override { check(); }
    };
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    const auto response = netease::detail::aes(
        R"({"code":200,"data":[{"id":1,"code":200,"url":"https://m802.music.126.net/synthetic.mp3","level":"standard","type":"mp3","expi":1200,"size":8,"freeTrialInfo":null}]})",
        "e82ckenh8dichen8", EVP_aes_128_ecb(), true);
    QVERIFY(response);
    const auto response2 = netease::detail::aes(
        R"({"code":200,"data":[{"id":2,"code":200,"url":"https://m802.music.126.net/synthetic.mp3","level":"standard","type":"mp3","expi":1200,"size":8,"freeTrialInfo":null}]})",
        "e82ckenh8dichen8", EVP_aes_128_ecb(), true);
    QVERIFY(response2);
    const Plan lyric{R"({"code":200,"lrc":{"lyric":"[00:00]a\n[00:01]b"}})"};
    QList<Plan> plans;
    if (fm) {
        plans = loginPlans();
        plans.append(Plan{R"({"code":200,"data":[{"id":1},{"id":2},{"id":3}]})"});
        plans.append(Plan{"", 200, -1}); // low-water refill remains pending through close
    }
    plans.append(QList<Plan>{
        {*response, 200, 10}, lyric, {*response2, 200, 10}, lyric, {"", 200, -1}, {"", 200, -1}});
    auto f = factories(state, gate, plans);
    f.playback = [state, &gate] { return std::make_unique<Player>(state, gate); };
    BackendRuntime runtime(config, std::move(f));
    auto& facade = runtime.facade();
    QSignalSpy started(&facade, &BackendFacade::started), stopped(&facade, &BackendFacade::stopped);
    facade.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(facade.snapshot().token.isValid());
    auto& listening = runtime.listening();
    QVERIFY(listening.setVolume(0));
    if (fm) {
        QSignalSpy challenge(&facade, &BackendFacade::challengeReady),
            login(&facade, &BackendFacade::signedIn);
        QVERIFY(facade.beginLogin());
        QTRY_COMPARE(challenge.size(), 1);
        QVERIFY(facade.poll());
        QTRY_COMPARE(login.size(), 1);
        QVERIFY(listening.startFm(domain::AudioQuality::Standard, domain::TransportIntent::Pause));
    } else {
        QVERIFY(listening.replaceQueue({{"1"}, {"2"}, {"3"}}));
        QVERIFY(listening.pause());
    }
    QTRY_COMPARE(facade.playback().state, domain::PlaybackState::Paused);
    QCOMPARE(facade.playback().actualQuality, "standard");
    QTRY_COMPARE(listening.lyricsState(), domain::LyricsState::Ready);
    QVERIFY(listening.seek(1250));
    QTRY_COMPARE(facade.playback().positionMs, 1250);
    QTRY_COMPARE(listening.cursors().original.standard.lines, (QList<qsizetype>{1}));
    QVERIFY(listening.history().isEmpty()); // pause/seek position alone is not listening
    if (fm) {
        // A terminal notification observer pauses the NEXT load, without resurrecting this one.
        const auto pauseAtEnd = connect(&listening, &ListeningSession::playbackChanged, this,
                                        [&](domain::PlaybackSnapshot s) {
                                            if (s.state == domain::PlaybackState::Ended)
                                                QVERIFY(listening.pause());
                                        });
        QVERIFY(listening.resume());
        QTRY_COMPARE(facade.playback().state, domain::PlaybackState::Playing);
        QVERIFY(listening.setVolume(0.5f));
        emit gate.observedPlayback(500); // synthetic backend evidence, not a device test
        QTRY_COMPARE(listening.history().size(), 1);
        emit gate.endPlayback();
        QTRY_COMPARE(facade.playback().song.value, "2");
        disconnect(pauseAtEnd);
    } else
        QVERIFY(listening.next()); // atomic initial Pause, not Play then Pause
    QTRY_COMPARE(facade.playback().state, domain::PlaybackState::Paused);
    QTRY_COMPARE(listening.lyricsState(), domain::LyricsState::Ready);
    QCOMPARE(listening.lyrics()->song.value, "2");
    QVERIFY(listening.resume());
    QTRY_COMPARE(facade.playback().state, domain::PlaybackState::Playing);
    QVERIFY(listening.setVolume(0.5f));
    emit gate.observedPlayback(1000);
    QTRY_COMPARE(listening.history().size(), fm ? 2 : 1);
    QCOMPARE(listening.history().first().playedMs, 1000);
    QCOMPARE(listening.history().first().song.value, "2");
    QCOMPARE(listening.history().first().source,
             fm ? domain::ListeningMode::PersonalFm : domain::ListeningMode::Queue);
    QCOMPARE(listening.history().first().accountId, fm ? QString("101") : QString{});
    QVERIFY(listening.next());
    QTRY_COMPARE(state->requests.load(), fm ? 11 : 6);
    facade.close();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(facade.playback().state, domain::PlaybackState::Empty);
    QVERIFY(listening.queue().entries.isEmpty());
    QVERIFY(!listening.lyrics());
    QVERIFY(listening.history().isEmpty());
    QCOMPARE(listening.mode(), domain::ListeningMode::Queue);
    QVERIFY(!runtime.fm().snapshot().active);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::musicQueriesThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    seed(config.databasePath, *state);
    Gate gate;
    const QList<Plan> plans{
        Plan{R"({"code":200,"result":{"songs":[],"songCount":0}})"},
        Plan{R"({"code":200,"songs":[{"id":1}]})"},
        Plan{
            R"({"code":200,"yrc":{"lyric":"[1000,500](1000,500,0)x"},"ytlrc":{"lyric":"[00:01]t"}})"},
        Plan{R"({"code":200,"data":{"artist":{"id":1,"name":"synthetic"}}})"},
        Plan{R"({"code":200,"playlist":{"id":1,"trackCount":1,"trackIds":[{"id":2}]}})"},
        Plan{R"({"code":200,"songs":[{"id":2}]})"},
        Plan{"{}", 200, -1}};
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    QSignalSpy done(&f, &BackendFacade::musicCompleted);
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    QVERIFY(f.searchSongs({QStringLiteral("fixture")}) != 0);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(std::holds_alternative<domain::SongSearchPage>(
        qvariant_cast<MusicResult>(done.last().at(2))));
    QVERIFY(f.songDetails({{QStringLiteral("1")}}) != 0);
    QTRY_COMPARE(done.size(), 2);
    QVERIFY(
        std::holds_alternative<domain::SongDetails>(qvariant_cast<MusicResult>(done.last().at(2))));
    QVERIFY(f.lyrics({QStringLiteral("1")}) != 0);
    QTRY_COMPARE(done.size(), 3);
    const auto lyrics = std::get<domain::SongLyrics>(qvariant_cast<MusicResult>(done.last().at(2)));
    QCOMPARE(lyrics.original.enhanced.timing, domain::LyricTiming::Words);
    QCOMPARE(lyrics.translation.enhanced.timing, domain::LyricTiming::Lines);
    QVERIFY(f.artistDetails({"1"}));
    QTRY_COMPARE(done.size(), 4);
    QCOMPARE(
        std::get<domain::ArtistDetails>(qvariant_cast<MusicResult>(done.last().at(2))).id.value,
        "1");
    QVERIFY(f.playlistSongs({{"1"}, {10, 0}}));
    QTRY_COMPARE(done.size(), 5);
    const auto page =
        std::get<domain::PlaylistSongsPage>(qvariant_cast<MusicResult>(done.last().at(2)));
    QCOMPARE(page.songs.first().id.value, "2");
    QVERIFY(!page.hasMore);
    QVERIFY(f.lyrics({QStringLiteral("2")}) != 0);
    QTRY_COMPARE(state->requests.load(), 7);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 6);
    QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(done.last().at(2))).error,
             MusicError::Canceled);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::libraryThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic-library")};
    seed(config.databasePath, *state);
    Gate gate;
    auto plans = loginPlans();
    plans.append(
        Plan{R"({"code":200,"playlist":[{"id":1,"creator":{"userId":101}}],"more":false})"});
    plans.append(Plan{R"({"code":200,"ids":["9007199254740993",2]})"});
    plans.append(Plan{R"({"code":200,"data":[{"id":3}],"count":1,"hasMore":false})"});
    plans.append(Plan{R"({"code":200,"data":[{"id":4}],"count":1,"hasMore":false})"});
    plans.append(Plan{"{}", 200, -1});
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    QSignalSpy challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn);
    QSignalSpy done(&f, &BackendFacade::musicCompleted);
    bool wrongThread = false;
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                wrongThread |= QThread::currentThread() != qApp->thread();
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QCOMPARE(f.myPlaylists(), 0);
    QCOMPARE(f.likedSongIds(), 0);
    QCOMPARE(f.savedAlbums(), 0);
    QCOMPARE(f.savedArtists(), 0);
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.beginLogin());
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll());
    QTRY_COMPARE(login.size(), 1);
    const auto token = f.snapshot().token;
    QVERIFY(f.myPlaylists({1, 2}));
    QTRY_COMPARE(done.size(), 1);
    const auto playlists =
        std::get<domain::MyPlaylistsPage>(qvariant_cast<MusicResult>(done.last().at(2)));
    QCOMPARE(playlists.query.accountId, "101");
    QCOMPARE(playlists.query.page.offset, 2);
    QCOMPARE(playlists.playlists.first().creatorId, std::optional<QString>("101"));
    QVERIFY(f.likedSongIds());
    QTRY_COMPARE(done.size(), 2);
    QCOMPARE(std::get<domain::LikedSongIds>(qvariant_cast<MusicResult>(done.last().at(2)))
                 .ids.first()
                 .value,
             "9007199254740993");
    QVERIFY(f.savedAlbums());
    QTRY_COMPARE(done.size(), 3);
    QCOMPARE(std::get<domain::SavedAlbumsPage>(qvariant_cast<MusicResult>(done.last().at(2)))
                 .albums.first()
                 .id.value,
             "3");
    QVERIFY(f.savedArtists());
    QTRY_COMPARE(done.size(), 4);
    QCOMPARE(std::get<domain::SavedArtistsPage>(qvariant_cast<MusicResult>(done.last().at(2)))
                 .artists.first()
                 .id.value,
             "4");
    QVERIFY(f.likedSongIds());
    QTRY_COMPARE(state->requests.load(), 8);
    f.close();
    QCOMPARE(f.myPlaylists(), 0);
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 5);
    QVERIFY(std::holds_alternative<MusicFailure>(qvariant_cast<MusicResult>(done.last().at(2))));
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last().at(1)), token);
    QVERIFY(!wrongThread);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::discoveryThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic-discovery")};
    seed(config.databasePath, *state);
    Gate gate;
    auto plans = loginPlans();
    plans.append(Plan{R"({"code":200,"data":{"dailySongs":[{"id":1,"reason":"synthetic"}]}})"});
    plans.append(Plan{R"({"code":200,"data":[{"id":2},{"id":3}]})"});
    plans.append(Plan{"{}", 200, -1});
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    auto& fm = runtime.fm();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    QSignalSpy challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn);
    QSignalSpy done(&f, &BackendFacade::musicCompleted);
    bool wrongThread = false;
    connect(&fm, &PersonalFmSource::changed, this,
            [&](PersonalFmSnapshot) { wrongThread |= QThread::currentThread() != qApp->thread(); });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QCOMPARE(f.dailyRecommendations(), 0);
    QVERIFY(!fm.start());
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.beginLogin());
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll());
    QTRY_COMPARE(login.size(), 1);
    QVERIFY(f.dailyRecommendations());
    QTRY_COMPARE(done.size(), 1);
    const auto daily =
        std::get<domain::DailyRecommendations>(qvariant_cast<MusicResult>(done.last().at(2)));
    QCOMPARE(daily.songs.first().song.id.value, "1");
    QVERIFY(daily.receivedAtUtc.isValid());
    QVERIFY(fm.start());
    QTRY_COMPARE(fm.snapshot().upcoming.size(), 2);
    QCOMPARE(fm.takeNext()->id.value, "2");
    QTRY_COMPARE(state->requests.load(), 6);
    QVERIFY(fm.snapshot().fetching);
    f.close();
    QVERIFY(fm.snapshot().upcoming.isEmpty());
    QTRY_COMPARE(stopped.size(), 1);
    QVERIFY(!fm.snapshot().active);
    QVERIFY(!wrongThread);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::startLoginCloseRestore()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic-cirrus")};
    auto state = std::make_shared<SharedState>();
    seed(config.databasePath, *state);
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate, loginPlans()));
    auto& facade = runtime.facade();
    QSignalSpy started(&facade, &BackendFacade::started), stopped(&facade, &BackendFacade::stopped);
    QSignalSpy challenge(&facade, &BackendFacade::challengeReady),
        login(&facade, &BackendFacade::signedIn);
    QSignalSpy saved(&facade, &BackendFacade::savedSessionCompleted);
    bool wrongDeliveryThread = false;
    connect(&facade, &BackendFacade::sessionChanged, this, [&](domain::SessionSnapshot) {
        wrongDeliveryThread = wrongDeliveryThread || QThread::currentThread() != qApp->thread();
    });
    facade.start();
    facade.start();
    QTRY_COMPARE(started.size(), 1);
    QCOMPARE(qvariant_cast<StartupError>(started.first().first()), StartupError::None);
    QCOMPARE(state->requests.load(), 0);
    QrLoginOptions options;
    options.rememberSession = true;
    QVERIFY(facade.beginLogin(options) != 0);
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(facade.poll() != 0);
    QTRY_COMPARE(login.size(), 1);
    QTRY_COMPARE(saved.size(), 1);
    QCOMPARE(facade.snapshot().phase, domain::SessionPhase::Authenticated);
    facade.close();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(stopped.first().first().toBool(), false);
    QVERIFY(state->contains(QStringLiteral("account-session"))); // exit is not logout
    QCOMPARE(state->created.load(), state->destroyed.load());
    QCOMPARE(state->wrongThread.load(), 0);
    QVERIFY(!wrongDeliveryThread);

    // Successful reopen proves SQL lock released and persistent identity remains usable.
    BackendRuntime reopened(config, factories(state, gate, {loginPlans().last()}));
    auto& next = reopened.facade();
    QSignalSpy readyAgain(&next, &BackendFacade::started),
        signedAgain(&next, &BackendFacade::signedIn);
    QSignalSpy closedAgain(&next, &BackendFacade::stopped);
    next.start();
    QTRY_COMPARE(readyAgain.size(), 1);
    QVERIFY(next.restore() != 0);
    QTRY_COMPARE(signedAgain.size(), 1);
    QCOMPARE(next.snapshot().accountId, QStringLiteral("101"));
    next.close();
    QTRY_COMPARE(closedAgain.size(), 1);
    QCOMPARE(state->requests.load(), 4); // QR key/check/identity + restored identity only
}

void BackendRuntimeTest::closeDuringRead()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    seed(config.databasePath, *state);
    state->holdReads = true;
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate));
    auto& f = runtime.facade();
    QSignalSpy stopped(&f, &BackendFacade::stopped), started(&f, &BackendFacade::started);
    QSignalSpy delayed(&f, &BackendFacade::closeDelayed);
    f.start();
    QTRY_COMPARE(state->reads.load(), 1);
    f.close();
    QTRY_COMPARE_WITH_TIMEOUT(delayed.size(), 1, 6500);
    QCOMPARE(stopped.size(), 0);
    QCOMPARE(f.state(), BackendState::Stopping);
    bool ticked = false;
    QTimer::singleShot(0, this, [&] { ticked = true; });
    QTRY_VERIFY(ticked); // caller event loop is not blocked on keyring/worker wait()
    emit gate.release();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(started.size(), 0);
    QCOMPARE(state->requests.load(),
             0); // late key read cannot register anonymous or revive startup
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::closeDuringAnonymousWrite()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    seed(config.databasePath, *state);
    state->records.remove(QStringLiteral("anonymous-token"));
    state->holdWrites = true;
    Plan anon{
        QByteArray::fromHex("e879bf9ce6ae320a55d1da6ac1d8b823a871f700a5a8fa0ea80d444039c2aa09")};
    anon.headers.append(std::pair<QByteArray, QByteArray>{
        "Set-Cookie", "MUSIC_A=synthetic-new; Domain=.music.163.com; Path=/"});
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate, {anon}));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    f.start();
    QTRY_COMPARE(state->writes.load(), 1);
    f.close();
    QTest::qWait(30);
    QCOMPARE(stopped.size(), 0);
    emit gate.release();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(started.size(), 0);
    QVERIFY(
        state->contains(QStringLiteral("anonymous-token"))); // canceled consumer != canceled write
    QCOMPARE(state->wrongThread.load(), 0);
}

void BackendRuntimeTest::anonymousPersistenceFailureIsReported()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    seed(config.databasePath, *state);
    state->records.remove(QStringLiteral("anonymous-token"));
    state->failWrite = true;
    Plan anon{
        QByteArray::fromHex("e879bf9ce6ae320a55d1da6ac1d8b823a871f700a5a8fa0ea80d444039c2aa09")};
    anon.headers.append(std::pair<QByteArray, QByteArray>{
        "Set-Cookie", "MUSIC_A=synthetic-new; Domain=.music.163.com; Path=/"});
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate, {anon}));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    QSignalSpy warning(&f, &BackendFacade::persistenceWarning);
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QCOMPARE(qvariant_cast<StartupError>(started.first().first()), StartupError::None);
    QCOMPARE(warning.size(), 1);
    QCOMPARE(qvariant_cast<PersistenceArea>(warning.first().first()),
             PersistenceArea::AnonymousToken);
    QVERIFY(!state->contains(QStringLiteral("anonymous-token")));
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(stopped.first().first().toBool(), true);
}

void BackendRuntimeTest::closeDuringQrRequest()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    seed(config.databasePath, *state);
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate, {Plan{"{}", 200, -1}}));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    QSignalSpy challenge(&f, &BackendFacade::challengeReady);
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QVERIFY(f.beginLogin() != 0);
    QTRY_COMPARE(state->requests.load(), 1);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(challenge.size(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
    QCOMPARE(state->wrongThread.load(), 0);
}

void BackendRuntimeTest::closeWhileDatabaseOpening()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate));
    auto& f = runtime.facade();
    QSignalSpy stopped(&f, &BackendFacade::stopped), started(&f, &BackendFacade::started);
    f.start();
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(started.size(), 0);
    QCOMPARE(state->requests.load(), 0);
    // Must be released even when shutdown overtakes DatabaseWorker::opened().
    storage::DatabaseWorker db(config.databasePath, storage::InstallationStore::migrations());
    QSignalSpy opened(&db, &storage::DatabaseWorker::opened);
    db.open();
    QVERIFY(opened.wait(3000));
    QCOMPARE(qvariant_cast<storage::StorageError>(opened.first().first()),
             storage::StorageError::None);
}

void BackendRuntimeTest::closeDrainsWrite_data()
{
    QTest::addColumn<bool>("logout");
    QTest::addColumn<bool>("failDelete");
    QTest::newRow("exit-preserves-write") << false << false;
    QTest::newRow("logout-drains-then-deletes") << true << false;
    QTest::newRow("failed-delete-reported") << true << true;
}

void BackendRuntimeTest::closeDrainsWrite()
{
    QFETCH(bool, logout);
    QFETCH(bool, failDelete);
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    seed(config.databasePath, *state);
    state->holdWrites = true;
    state->failRemove = failDelete;
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate, loginPlans()));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    QSignalSpy challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn);
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QrLoginOptions options;
    options.rememberSession = true;
    QVERIFY(f.beginLogin(options) != 0);
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll() != 0);
    QTRY_COMPARE(login.size(), 1);
    QTRY_COMPARE(state->writes.load(), 1);
    if (logout)
        QVERIFY(f.signOut() != 0);
    f.close();
    QTest::qWait(30);
    QCOMPARE(stopped.size(), 0);
    QCOMPARE(state->removes.load(), 0); // delete cannot overtake outstanding write
    emit gate.release();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(stopped.first().first().toBool(), failDelete);
    QCOMPARE(state->contains(QStringLiteral("account-session")), !logout || failDelete);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::destructionStillDrains()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    const BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                               QStringLiteral("synthetic")};
    seed(config.databasePath, *state);
    state->holdReads = true;
    Gate gate;
    auto runtime = std::make_unique<BackendRuntime>(config, factories(state, gate));
    runtime->facade().start();
    QTRY_COMPARE(state->reads.load(), 1);
    runtime.reset(); // must not destroy a running QThread or block caller
    QTest::qWait(30);
    QVERIFY(state->destroyed.load() < state->created.load());
    emit gate.release();
    QTRY_COMPARE(state->destroyed.load(), state->created.load());
    QCOMPARE(state->wrongThread.load(), 0);
}

void BackendRuntimeTest::startupFailure_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("invalid-config") << 0;
    QTest::newRow("database-lock") << 1;
    QTest::newRow("guest-network-degraded") << 2;
}

void BackendRuntimeTest::startupFailure()
{
    QFETCH(int, mode);
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    BackendConfig config{directory.filePath(QStringLiteral("state.sqlite")),
                         QStringLiteral("synthetic")};
    std::unique_ptr<storage::DatabaseWorker> held;
    if (mode == 0)
        config.databasePath = QStringLiteral("relative.sqlite");
    if (mode == 1) {
        held = std::make_unique<storage::DatabaseWorker>(config.databasePath,
                                                         storage::InstallationStore::migrations());
        QSignalSpy opened(held.get(), &storage::DatabaseWorker::opened);
        held->open();
        QVERIFY(opened.wait(3000));
    }
    Gate gate;
    BackendRuntime runtime(config, factories(state, gate));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QCOMPARE(f.state(), mode == 2 ? BackendState::Ready : BackendState::Failed);
    QCOMPARE(qvariant_cast<StartupError>(started.first().first()),
             mode == 0   ? StartupError::Configuration
             : mode == 1 ? StartupError::Storage
                         : StartupError::GuestBootstrap);
    QCOMPARE(state->requests.load(), mode == 2 ? 1 : 0);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
}

void BackendRuntimeTest::closeBeforeStart()
{
    BackendRuntime runtime({}, {});
    auto& f = runtime.facade();
    QSignalSpy stopped(&f, &BackendFacade::stopped);
    f.close();
    f.start();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(f.state(), BackendState::Stopped);
}

void BackendRuntimeTest::trackedStoreKeepsDeadRecipientsAccounted()
{
    auto backend = std::make_unique<FakeSecretStore>();
    auto* fake = backend.get();
    fake->hold = true;
    TrackedSecretStore tracked(std::move(backend));
    QSignalSpy idle(&tracked, &TrackedSecretStore::idle);
    auto recipient = std::make_unique<QObject>();
    bool delivered = false;
    tracked.write(QStringLiteral("synthetic"), "synthetic-data", recipient.get(),
                  [&](SecretError) { delivered = true; });
    recipient.reset();
    QCOMPARE(tracked.pending(), 1);
    fake->release();
    QCOMPARE(tracked.pending(), 0);
    QCOMPARE(idle.size(), 1);
    QVERIFY(!delivered);
    QVERIFY(fake->entries.contains(QStringLiteral("synthetic")));
}

void BackendRuntimeTest::listeningReportThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    auto plans = loginPlans();
    plans.append(QList<Plan>{Plan{R"({"code":200})"}, Plan{R"({"code":500})"}, Plan{"", 200, -1}});
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped);
    QSignalSpy challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn);
    QSignalSpy reports(&f, &BackendFacade::listeningReportCompleted);
    connect(&f, &BackendFacade::listeningReportCompleted, this,
            [&](quint64, domain::SessionToken, domain::ListeningReportResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    domain::ListeningReport r{
        QUuid::createUuid(), {"1"}, {"2"}, domain::ListeningReportAction::StartPlay, 0};
    QVERIFY(!f.reportListening(r));
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.beginLogin());
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll());
    QTRY_COMPARE(login.size(), 1);
    QCOMPARE(state->requests.load(), 3); // startup/login do not automatically report
    const auto context = f.snapshot().token;
    QVERIFY(f.reportListening(r));
    QTRY_COMPARE(reports.size(), 1);
    QCOMPARE(qvariant_cast<domain::ListeningReportResult>(reports[0][2]).outcome,
             domain::ReportOutcome::Acknowledged);
    QCOMPARE(state->requests.load(), 4); // no implicit play action
    r.action = domain::ListeningReportAction::Play;
    r.playedSeconds = 35;
    QVERIFY(f.reportListening(r));
    QTRY_COMPARE(reports.size(), 2);
    QCOMPARE(qvariant_cast<domain::ListeningReportResult>(reports[1][2]).outcome,
             domain::ReportOutcome::Rejected);
    QCOMPARE(qvariant_cast<domain::ListeningReportResult>(reports[0][2]).outcome,
             domain::ReportOutcome::Acknowledged);
    QCOMPARE(state->requests.load(), 5); // a failed play does not erase acknowledged start
    r.eventId = QUuid::createUuid();
    QVERIFY(f.reportListening(r));
    QTRY_COMPARE(state->requests.load(), 6);
    f.close();
    QTRY_COMPARE(reports.size(), 3);
    QTRY_COMPARE(stopped.size(), 1);
    const auto last = qvariant_cast<domain::ListeningReportResult>(reports.last()[2]);
    QCOMPARE(last.outcome, domain::ReportOutcome::Unknown);
    QCOMPARE(last.error, domain::ReportError::Canceled);
    QCOMPARE(qvariant_cast<domain::SessionToken>(reports.last()[1]), context);
    QCOMPARE(state->requests.load(), 6); // close does not flush/retry reports
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::cloudHistoryThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    auto plans = loginPlans();
    plans.append(QList<Plan>{
        Plan{
            R"({"code":200,"data":{"total":4,"list":[{"resourceId":"9007199254740993","resourceType":"SONG","playTime":1730000000000,"data":{"id":"9007199254740993"}},{"data":{"id":"9007199254740993"}}]}})"},
        Plan{R"({"code":200,"weekData":[{"song":{"id":2},"playCount":3,"score":87.5}]})"},
        Plan{R"({"code":200,"allData":[{"song":{"id":3},"playCount":20}]})"}, Plan{"", 200, -1}});
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn),
        done(&f, &BackendFacade::musicCompleted),
        reports(&f, &BackendFacade::listeningReportRequested);
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    QVERIFY(!f.recentSongs());
    QVERIFY(!f.listeningRanking());
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.beginLogin());
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll());
    QTRY_COMPARE(login.size(), 1);
    const auto context = f.snapshot().token;
    const auto recentId = f.recentSongs(2);
    QVERIFY(recentId);
    QTRY_COMPARE(done.size(), 1);
    const auto recentResult = qvariant_cast<MusicResult>(done[0][2]);
    QVERIFY(std::holds_alternative<domain::CloudRecentSongs>(recentResult));
    const auto recent = std::get<domain::CloudRecentSongs>(recentResult);
    QCOMPARE(recent.query.accountId, "101");
    QCOMPARE(recent.query.limit, 2);
    QCOMPARE(recent.songs.size(), 2);
    QCOMPARE(recent.songs.first().song.id.value, "9007199254740993");
    QCOMPARE(recent.songs.first().providerPlayTime, std::optional<qint64>(1730000000000LL));
    QVERIFY(!recent.songs.last().providerPlayTime);
    QCOMPARE(recent.total, std::optional<qint64>(4));
    QVERIFY(f.listeningRanking(domain::RankingPeriod::LastWeek));
    QTRY_COMPARE(done.size(), 2);
    const auto weekResult = qvariant_cast<MusicResult>(done[1][2]);
    QVERIFY(std::holds_alternative<domain::ListeningRanking>(weekResult));
    const auto week = std::get<domain::ListeningRanking>(weekResult);
    QCOMPARE(week.query.period, domain::RankingPeriod::LastWeek);
    QCOMPARE(week.songs.first().playCount, std::optional<qint64>(3));
    QCOMPARE(week.songs.first().score, std::optional<double>(87.5));
    QVERIFY(f.listeningRanking());
    QTRY_COMPARE(done.size(), 3);
    const auto allResult = qvariant_cast<MusicResult>(done[2][2]);
    QVERIFY(std::holds_alternative<domain::ListeningRanking>(allResult));
    const auto all = std::get<domain::ListeningRanking>(allResult);
    QCOMPARE(all.query.period, domain::RankingPeriod::AllTime);
    QCOMPARE(all.songs.first().song.id.value, "3");
    QVERIFY(!all.songs.first().score);
    const auto pending = f.recentSongs();
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 7);
    f.close();
    QTRY_COMPARE(done.size(), 4);
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last()[1]), context);
    const auto canceled = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<MusicFailure>(canceled));
    QCOMPARE(std::get<MusicFailure>(canceled).error, MusicError::Canceled);
    QVERIFY(reports.isEmpty());
    QCOMPARE(state->requests.load(), 7);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::chartsToPlaylistPlaybackThroughThread()
{
    // Synthetic decoder only. No real media backend/CDN, accounts or device audio.
    class Player final : public IPlaybackBackend
    {
      public:
        std::shared_ptr<SharedState> state;
        domain::LoadToken token;
        explicit Player(std::shared_ptr<SharedState> s) : state(std::move(s))
        {
            check();
            ++state->created;
        }
        ~Player() override
        {
            check();
            ++state->destroyed;
        }
        void check()
        {
            if (QThread::currentThread() == qApp->thread())
                ++state->wrongThread;
        }
        void load(domain::StreamSource, domain::LoadToken t) override
        {
            check();
            token = t;
            QTimer::singleShot(0, this, [this, t] {
                emit event({t, PlaybackEvent::Kind::Duration, {}, 3000});
                emit event({t, PlaybackEvent::Kind::Seekable, {}, 1});
                emit event({t, PlaybackEvent::Kind::Ready});
            });
        }
        void play() override
        {
            check();
            emit event({token, PlaybackEvent::Kind::State, domain::PlaybackState::Playing});
        }
        void pause() override { check(); }
        void stop() override { check(); }
        void seek(qint64) override { check(); }
        void setVolume(float) override { check(); }
    };
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    const Plan index{
        R"({"code":200,"playlist":{"id":"9007199254740993","name":"synthetic chart","trackCount":3,"trackIds":[{"id":1},{"id":2},{"id":1}],"tracks":[{"id":1}]}})"};
    const auto stream = netease::detail::aes(
        R"({"code":200,"data":[{"id":2,"code":200,"url":"https:\/\/m802.music.126.net\/synthetic.mp3","level":"standard","type":"mp3","expi":1200,"size":8,"freeTrialInfo":null}]})",
        "e82ckenh8dichen8", EVP_aes_128_ecb(), true);
    QVERIFY(stream);
    auto options = factories(
        state, gate,
        {Plan{R"({"code":200,"list":[{"id":"9007199254740993","name":"synthetic chart"}]})"},
         Plan{
             R"({"code":200,"list":[{"id":"9007199254740993","updateFrequency":"provider label","tracks":[{"first":"display only, no song ID"}]}]})"},
         index, index, Plan{R"({"code":200,"songs":[{"id":2},{"id":1}]})"}, Plan{*stream, 200, 10},
         Plan{R"({"code":200,"lrc":{"lyric":"[00:00]synthetic"}})"}, Plan{"", 200, -1}});
    options.playback = [state] { return std::make_unique<Player>(state); };
    BackendRuntime runtime(config, std::move(options));
    auto& f = runtime.facade();
    auto& listening = runtime.listening();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        done(&f, &BackendFacade::musicCompleted),
        reports(&f, &BackendFacade::listeningReportRequested),
        play(&f, &BackendFacade::playbackRequested);
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    QCOMPARE(f.snapshot().phase, domain::SessionPhase::Guest);
    const auto context = f.snapshot().token;
    QVERIFY(f.charts());
    QTRY_COMPARE(done.size(), 1);
    const auto overviewResult = qvariant_cast<MusicResult>(done[0][2]);
    QVERIFY(std::holds_alternative<domain::Charts>(overviewResult));
    const auto overview = std::get<domain::Charts>(overviewResult);
    QCOMPARE(overview.charts.size(), 1);
    const auto playlist = overview.charts.first().playlist;
    QCOMPARE(playlist.value, "9007199254740993");
    QVERIFY(!overview.charts.first().previews);
    QVERIFY(f.charts(domain::ChartListKind::WithPreviews));
    QTRY_COMPARE(done.size(), 2);
    const auto previewResult = qvariant_cast<MusicResult>(done[1][2]);
    QVERIFY(std::holds_alternative<domain::Charts>(previewResult));
    const auto previews = std::get<domain::Charts>(previewResult);
    QCOMPARE(previews.query.kind, domain::ChartListKind::WithPreviews);
    QVERIFY(previews.charts.first().previews);
    QCOMPARE(previews.charts.first().previews->first().primaryText, "display only, no song ID");
    QVERIFY(f.playlistDetails(playlist));
    QTRY_COMPARE(done.size(), 3);
    const auto detailResult = qvariant_cast<MusicResult>(done[2][2]);
    QVERIFY(std::holds_alternative<domain::PlaylistDetails>(detailResult));
    const auto details = std::get<domain::PlaylistDetails>(detailResult);
    QVERIFY(details.trackIdsComplete);
    QCOMPARE(details.songs.size(), 1);
    QVERIFY(f.playlistSongs({playlist, {3, 0}}));
    QTRY_COMPARE(done.size(), 4);
    const auto pageResult = qvariant_cast<MusicResult>(done[3][2]);
    QVERIFY(std::holds_alternative<domain::PlaylistSongsPage>(pageResult));
    const auto page = std::get<domain::PlaylistSongsPage>(pageResult);
    QCOMPARE(page.query.playlist.value, playlist.value);
    QCOMPARE(page.ids, (QList<domain::SongId>{{"1"}, {"2"}, {"1"}}));
    QCOMPARE(page.songs.size(), 3);
    QVERIFY(page.missing.isEmpty());
    QCOMPARE(page.songs[0].id.value, "1");
    QCOMPARE(page.songs[1].id.value, "2");
    QCOMPARE(page.songs[2].id.value, "1");
    QCOMPARE(state->requests.load(), 5);
    QVERIFY(play.isEmpty());
    QVERIFY(listening.queue().entries.isEmpty());
    // Explicit caller selection only: real hydrated SongId, not playlist ID or preview text.
    QVERIFY(listening.append({page.songs[1].id}));
    QCOMPARE(listening.queue().entries.size(), 1);
    QVERIFY(play.isEmpty());
    QVERIFY(listening.select(listening.queue().entries.first().id));
    QTRY_COMPARE(f.playback().state, domain::PlaybackState::Playing);
    QTRY_COMPARE(listening.lyricsState(), domain::LyricsState::Ready);
    QCOMPARE(f.playback().song.value, "2");
    QCOMPARE(f.playback().token.session, context);
    QCOMPARE(state->requests.load(), 7);
    QVERIFY(reports.isEmpty());
    const auto pending = f.charts(domain::ChartListKind::WithPreviews);
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 8);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 6);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last()[1]), context);
    const auto canceled = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<MusicFailure>(canceled));
    QCOMPARE(std::get<MusicFailure>(canceled).error, MusicError::Canceled);
    QCOMPARE(f.playback().state, domain::PlaybackState::Empty);
    QVERIFY(listening.queue().entries.isEmpty());
    QVERIFY(reports.isEmpty());
    QCOMPARE(state->requests.load(), 8);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::playlistDiscoveryThroughThread()
{
    class Player final : public IPlaybackBackend
    {
      public:
        std::shared_ptr<SharedState> state;
        domain::LoadToken token;
        explicit Player(std::shared_ptr<SharedState> s) : state(std::move(s))
        {
            check();
            ++state->created;
        }
        ~Player() override
        {
            check();
            ++state->destroyed;
        }
        void check()
        {
            if (QThread::currentThread() == qApp->thread())
                ++state->wrongThread;
        }
        void load(domain::StreamSource, domain::LoadToken t) override
        {
            check();
            token = t;
            QTimer::singleShot(0, this, [this, t] { emit event({t, PlaybackEvent::Kind::Ready}); });
        }
        void play() override
        {
            check();
            emit event({token, PlaybackEvent::Kind::State, domain::PlaybackState::Playing});
        }
        void pause() override { check(); }
        void stop() override { check(); }
        void seek(qint64) override { check(); }
        void setVolume(float) override { check(); }
    };
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    const Plan index{
        R"({"code":200,"playlist":{"id":"9007199254740993","trackCount":2,"trackIds":[{"id":1},{"id":2}],"tracks":[{"id":1}]}})"};
    const auto stream = netease::detail::aes(
        R"({"code":200,"data":[{"id":2,"code":200,"url":"https:\/\/m802.music.126.net\/synthetic.mp3","level":"standard","type":"mp3","expi":1200,"size":8,"freeTrialInfo":null}]})",
        "e82ckenh8dichen8", EVP_aes_128_ecb(), true);
    QVERIFY(stream);
    auto options = factories(
        state, gate,
        {Plan{
             R"({"code":200,"result":[{"id":"9007199254740993","copywriter":"synthetic recommendation"}]})"},
         Plan{R"({"code":200,"playlists":[{"id":3}],"more":true,"total":5})"},
         Plan{R"({"code":200,"playlists":[{"id":4,"updateTime":100}],"more":true,"lasttime":200})"},
         Plan{
             R"({"code":200,"sub":[{"name":"华语","category":0}],"all":{"name":"全部"},"categories":{"0":"语种"}})"},
         Plan{R"({"code":200,"tags":[{"name":"流行","hot":false}]})"},
         Plan{R"({"code":200,"tags":[{"name":"爵士"}]})"},
         Plan{R"({"code":200,"playlists":[{"id":5,"updateTime":50}],"more":false})"}, index, index,
         Plan{R"({"code":200,"songs":[{"id":2},{"id":1}]})"}, Plan{*stream, 200, 10},
         Plan{R"({"code":200,"lrc":{"lyric":"[00:00]synthetic"}})"}, Plan{"", 200, -1}});
    options.playback = [state] { return std::make_unique<Player>(state); };
    BackendRuntime runtime(config, std::move(options));
    auto& f = runtime.facade();
    auto& listening = runtime.listening();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
        reports(&f, &BackendFacade::listeningReportRequested);
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    QCOMPARE(f.snapshot().phase, domain::SessionPhase::Guest);
    const auto context = f.snapshot().token;
    QVERIFY(f.recommendedPlaylists(2));
    QTRY_COMPARE(done.size(), 1);
    const auto recResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::RecommendedPlaylists>(recResult));
    const auto rec = std::get<domain::RecommendedPlaylists>(recResult);
    QCOMPARE(rec.query.limit, 2);
    const auto playlist = rec.playlists.first().id;
    QCOMPARE(playlist.value, "9007199254740993");
    QVERIFY(!rec.playlists.first().trackCount);
    QVERIFY(f.playlistList({"R&B/Soul", {2, 10}, domain::PlaylistOrder::New}));
    QTRY_COMPARE(done.size(), 2);
    const auto topResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PlaylistListPage>(topResult));
    const auto top = std::get<domain::PlaylistListPage>(topResult);
    QCOMPARE(top.query.page.offset, 10);
    QCOMPARE(top.query.category, "R&B/Soul");
    QCOMPARE(top.hasMore, std::optional<bool>(true));
    QVERIFY(f.premiumPlaylists({"全部", 2, 0}));
    QTRY_COMPARE(done.size(), 3);
    const auto premiumResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PremiumPlaylistsPage>(premiumResult));
    const auto premium = std::get<domain::PremiumPlaylistsPage>(premiumResult);
    QCOMPARE(premium.query.before, 0);
    QCOMPARE(premium.providerLastTime, std::optional<qint64>(200));
    QCOMPARE(premium.playlists.last().providerUpdateTime, std::optional<qint64>(100));
    QVERIFY(f.playlistTags());
    QTRY_COMPARE(done.size(), 4);
    const auto catalogueResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PlaylistTags>(catalogueResult));
    const auto catalogue = std::get<domain::PlaylistTags>(catalogueResult);
    QVERIFY(catalogue.categories);
    QCOMPARE(catalogue.categories->value(0), QString("语种"));
    QVERIFY(f.playlistTags(domain::PlaylistTagsKind::Hot));
    QTRY_COMPARE(done.size(), 5);
    const auto hotResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PlaylistTags>(hotResult));
    QCOMPARE(std::get<domain::PlaylistTags>(hotResult).query.kind, domain::PlaylistTagsKind::Hot);
    QCOMPARE(std::get<domain::PlaylistTags>(hotResult).tags.first().hot,
             std::optional<bool>(false));
    QVERIFY(f.playlistTags(domain::PlaylistTagsKind::Premium));
    QTRY_COMPARE(done.size(), 6);
    const auto tagsResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PlaylistTags>(tagsResult));
    QCOMPARE(std::get<domain::PlaylistTags>(tagsResult).query.kind,
             domain::PlaylistTagsKind::Premium);
    QVERIFY(!std::get<domain::PlaylistTags>(tagsResult).categories);
    QCOMPARE(state->requests.load(), 6);
    QVERIFY(play.isEmpty());
    QVERIFY(listening.queue().entries.isEmpty());
    // Explicit before from the final row's updateTime, never offset or root.lasttime guessing.
    QVERIFY(f.premiumPlaylists(
        {premium.query.category, 2, *premium.playlists.last().providerUpdateTime}));
    QTRY_COMPARE(done.size(), 7);
    const auto nextResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PremiumPlaylistsPage>(nextResult));
    QCOMPARE(std::get<domain::PremiumPlaylistsPage>(nextResult).query.before, 100);
    QCOMPARE(std::get<domain::PremiumPlaylistsPage>(nextResult).hasMore,
             std::optional<bool>(false));
    QVERIFY(f.playlistDetails(playlist));
    QTRY_COMPARE(done.size(), 8);
    QVERIFY(std::holds_alternative<domain::PlaylistDetails>(
        qvariant_cast<MusicResult>(done.last()[2])));
    QVERIFY(f.playlistSongs({playlist, {2, 0}}));
    QTRY_COMPARE(done.size(), 9);
    const auto pageResult = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PlaylistSongsPage>(pageResult));
    const auto page = std::get<domain::PlaylistSongsPage>(pageResult);
    QCOMPARE(page.songs.size(), 2);
    QCOMPARE(page.songs[1].id.value, "2");
    QCOMPARE(state->requests.load(), 10);
    QVERIFY(play.isEmpty());
    QVERIFY(listening.append({page.songs[1].id}));
    QVERIFY(play.isEmpty());
    QVERIFY(listening.select(listening.queue().entries.first().id));
    QTRY_COMPARE(f.playback().state, domain::PlaybackState::Playing);
    QTRY_COMPARE(listening.lyricsState(), domain::LyricsState::Ready);
    QCOMPARE(f.playback().song.value, "2");
    QCOMPARE(state->requests.load(), 12);
    QVERIFY(reports.isEmpty());
    const auto pending = f.playlistTags();
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 13);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 11);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last()[1]), context);
    const auto canceled = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<MusicFailure>(canceled));
    QCOMPARE(std::get<MusicFailure>(canceled).error, MusicError::Canceled);
    QVERIFY(reports.isEmpty());
    QVERIFY(listening.queue().entries.isEmpty());
    QCOMPARE(state->requests.load(), 13);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
}

void BackendRuntimeTest::entitySearchThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    const auto cipher = netease::detail::aes(
        R"({"code":200,"data":{"resources":[{"resourceType":"voicelist","resourceId":"4","baseInfo":{"id":4}}],"totalCount":1}})",
        "e82ckenh8dichen8", EVP_aes_128_ecb(), true);
    QVERIFY(cipher);
    BackendRuntime runtime(
        config,
        factories(
            state, gate,
            {Plan{R"({"code":200,"result":{"albums":[{"id":"9007199254740993"}]}})"},
             Plan{R"({"code":200,"result":{"artists":[{"id":2}],"hasMore":true}})"},
             Plan{R"({"code":200,"result":{"playlists":[{"id":3}]}})"}, Plan{*cipher},
             Plan{
                 R"({"code":200,"data":{"resources":[{"resourceType":"voice","resourceId":"5","baseInfo":{"id":5,"mainSong":{"id":99},"radio":{"id":4}}}],"totalCount":1}})"},
             Plan{"", 200, -1}}));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
        reports(&f, &BackendFacade::listeningReportRequested);
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    const auto token = f.snapshot().token;
    QVERIFY(f.searchAlbums({"synthetic", 2, 0}));
    QTRY_COMPARE(done.size(), 1);
    const auto a = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::AlbumSearchPage>(a));
    QCOMPARE(std::get<domain::AlbumSearchPage>(a).albums.first().id.value, "9007199254740993");
    QVERIFY(f.searchArtists({"synthetic", 2, 0}));
    QTRY_COMPARE(done.size(), 2);
    const auto ar = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::ArtistSearchPage>(ar));
    QCOMPARE(std::get<domain::ArtistSearchPage>(ar).artists.first().id.value, "2");
    QVERIFY(f.searchPlaylists({"synthetic", 2, 0}));
    QTRY_COMPARE(done.size(), 3);
    const auto p = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PlaylistSearchPage>(p));
    QCOMPARE(std::get<domain::PlaylistSearchPage>(p).playlists.first().id.value, "3");
    QVERIFY(f.searchPodcasts({"synthetic", 2, 0}));
    QTRY_COMPARE(done.size(), 4);
    const auto pod = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PodcastSearchPage>(pod));
    QCOMPARE(std::get<domain::PodcastSearchPage>(pod).podcasts.first().id.value, "4");
    QVERIFY(f.searchVoices({"synthetic", 2, 0}));
    QTRY_COMPARE(done.size(), 5);
    const auto v = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::VoiceSearchPage>(v));
    const auto voice = std::get<domain::VoiceSearchPage>(v).voices.first();
    QCOMPARE(voice.id.value, "5");
    QVERIFY(voice.podcast);
    QCOMPARE(voice.podcast->id.value, "4");
    for (const auto& row : done)
        QCOMPARE(qvariant_cast<domain::SessionToken>(row[1]), token);
    QVERIFY(play.isEmpty());
    QVERIFY(reports.isEmpty());
    QVERIFY(runtime.listening().queue().entries.isEmpty());
    QCOMPARE(state->requests.load(), 5);
    const auto pending = f.searchVoices({"synthetic", 2, 0});
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 6);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 6);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    const auto canceled = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<MusicFailure>(canceled));
    QCOMPARE(std::get<MusicFailure>(canceled).error, MusicError::Canceled);
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last()[1]), token);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
    QVERIFY(play.isEmpty());
    QVERIFY(reports.isEmpty());
}

void BackendRuntimeTest::podcastReadsThroughThread()
{
    // Synthetic consumption/program candidates and account records, no live audio/account.
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    QList<Plan> plans{
        Plan{R"({"code":200,"data":{"id":4,"programCount":2}})"},
        Plan{
            R"({"code":200,"programs":[{"id":5,"radio":{"id":4},"mainSong":{"id":99}}],"count":2,"more":true})"},
        Plan{
            R"({"code":200,"program":{"id":5,"radio":{"id":4},"duration":2000,"mainTrackId":99}})"}};
    plans.append(loginPlans());
    plans.append(QList<Plan>{
        Plan{R"({"code":200,"data":{"list":[{"data":{"id":3}}],"total":5}})"},
        Plan{R"({"code":200,"data":{"list":[{"data":{"id":2},"playTime":0}]}})"},
        Plan{R"({"code":200,"data":{"list":[{"data":{"id":4}}]}})"},
        Plan{
            R"({"code":200,"data":{"list":[{"data":{"id":6,"mainSong":{"id":99},"radio":{"id":4}},"playTime":1730000000000}]}})"},
        Plan{"", 200, -1}});
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn),
        done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
        report(&f, &BackendFacade::listeningReportRequested);
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    const auto guest = f.snapshot().token;
    QVERIFY(!f.recentPlaylists());
    QVERIFY(!f.recentAlbums());
    QVERIFY(!f.recentPodcasts());
    QVERIFY(!f.recentVoices());
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.podcastDetails({"4"}));
    QTRY_COMPARE(done.size(), 1);
    const auto p = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PodcastDetails>(p));
    const auto pod = std::get<domain::PodcastDetails>(p).podcast.id;
    QCOMPARE(pod.value, "4");
    QVERIFY(f.podcastPrograms({pod, {2, 0}}));
    QTRY_COMPARE(done.size(), 2);
    const auto l = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::PodcastProgramsPage>(l));
    const auto list = std::get<domain::PodcastProgramsPage>(l);
    QCOMPARE(list.query.podcast.value, "4");
    QCOMPARE(list.total, std::optional<qint64>(2));
    QCOMPARE(list.hasMore, std::optional<bool>(true));
    const auto program = list.programs.first().id;
    QCOMPARE(program.value, "5");
    QVERIFY(f.programDetails(program));
    QTRY_COMPARE(done.size(), 3);
    const auto d = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::ProgramDetails>(d));
    QCOMPARE(std::get<domain::ProgramDetails>(d).program.id.value, "5");
    for (const auto& row : done)
        QCOMPARE(qvariant_cast<domain::SessionToken>(row[1]), guest);
    QVERIFY(f.beginLogin());
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll());
    QTRY_COMPARE(login.size(), 1);
    const auto account = f.snapshot().token;
    QVERIFY(account != guest);
    QVERIFY(f.recentPlaylists(2));
    QTRY_COMPARE(done.size(), 4);
    const auto pl = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CloudRecentPlaylists>(pl));
    QCOMPARE(std::get<domain::CloudRecentPlaylists>(pl).query.accountId, "101");
    QCOMPARE(std::get<domain::CloudRecentPlaylists>(pl).items.first().entity.id.value, "3");
    QVERIFY(f.recentAlbums(2));
    QTRY_COMPARE(done.size(), 5);
    const auto a = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CloudRecentAlbums>(a));
    QCOMPARE(std::get<domain::CloudRecentAlbums>(a).items.first().providerPlayTime,
             std::optional<qint64>(0));
    QVERIFY(f.recentPodcasts(2));
    QTRY_COMPARE(done.size(), 6);
    const auto rp = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CloudRecentPodcasts>(rp));
    QCOMPARE(std::get<domain::CloudRecentPodcasts>(rp).items.first().entity.id.value, "4");
    QVERIFY(f.recentVoices(2));
    QTRY_COMPARE(done.size(), 7);
    const auto rv = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CloudRecentVoices>(rv));
    QCOMPARE(std::get<domain::CloudRecentVoices>(rv).items.first().entity.id.value, "6");
    for (int i = 3; i < 7; ++i)
        QCOMPARE(qvariant_cast<domain::SessionToken>(done[i][1]), account);
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
    QVERIFY(runtime.listening().queue().entries.isEmpty());
    const auto pending = f.recentVoices(2);
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 11);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 8);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last()[1]), account);
    const auto c = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<MusicFailure>(c));
    QCOMPARE(std::get<MusicFailure>(c).error, MusicError::Canceled);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
}

void BackendRuntimeTest::commentsThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    const QList<Plan> plans{
        Plan{
            R"({"code":200,"comments":[{"commentId":10,"content":"synthetic","time":100}],"total":2,"more":true})"},
        Plan{R"({"code":200,"hotComments":[]})"},
        Plan{
            R"({"code":200,"data":{"comments":[],"cursor":"normalHot#0","sortType":2,"hasMore":true}})"},
        Plan{
            R"({"code":200,"data":{"comments":[{"commentId":11,"time":42}],"ownerComment":{"commentId":10},"totalCount":2,"hasMore":true}})"},
        Plan{
            R"({"code":200,"data":[{"resourceId":3,"threadId":"A_PL_0_3","resourceType":0,"commentCount":0,"liked":false}]})"},
        Plan{"", 200, -1}};
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
        report(&f, &BackendFacade::listeningReportRequested);
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    const auto t = f.snapshot().token;
    QVERIFY(!f.comments({domain::PodcastId{"5"}}));
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.comments({domain::SongId{"1"}, {2, 0}}));
    QTRY_COMPARE(done.size(), 1);
    const auto ordinary = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CommentsPage>(ordinary));
    QCOMPARE(std::get<domain::CommentsPage>(ordinary).nextBefore, std::optional<qint64>(100));
    QVERIFY(f.hotComments({domain::PodcastId{"5"}}));
    QTRY_COMPARE(done.size(), 2);
    QVERIFY(std::holds_alternative<domain::HotCommentsPage>(
        qvariant_cast<MusicResult>(done.last()[2])));
    QVERIFY(f.sortedComments({domain::AlbumId{"2"}, 2, 1, domain::CommentOrder::Hot, 0, false}));
    QTRY_COMPARE(done.size(), 3);
    const auto sorted = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::SortedCommentsPage>(sorted));
    QVERIFY(!std::get<domain::SortedCommentsPage>(sorted).query.showInner);
    QCOMPARE(std::get<domain::SortedCommentsPage>(sorted).providerCursor,
             std::optional<QString>("normalHot#0"));
    QVERIFY(f.commentFloor({domain::ProgramId{"4"}, {"10"}, 2, 0}));
    QTRY_COMPARE(done.size(), 4);
    const auto floor = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CommentFloorPage>(floor));
    QCOMPARE(std::get<domain::CommentFloorPage>(floor).nextTime, std::optional<qint64>(42));
    QVERIFY(f.commentInfo({{domain::PlaylistId{"3"}, domain::PlaylistId{"6"}}}));
    QTRY_COMPARE(done.size(), 5);
    const auto info = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CommentStatistics>(info));
    QCOMPARE(std::get<domain::CommentStatistics>(info).missing.size(), 1);
    QCOMPARE(state->requests.load(), 5); // no automatic page/floor/statistic request
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
    QVERIFY(runtime.listening().queue().entries.isEmpty());
    for (const auto& row : done)
        QCOMPARE(qvariant_cast<domain::SessionToken>(row[1]), t);
    const auto pending = f.commentFloor({domain::ProgramId{"4"}, {"10"}});
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 6);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 6);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last()[1]), t);
    QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(done.last()[2])).error,
             MusicError::Canceled);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
}

void BackendRuntimeTest::collectionsThroughThread()
{
    using namespace domain;
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    auto plans = loginPlans();
    for (int i = 0; i < 10; ++i)
        plans.append(Plan{i == 2 ? QByteArray(R"({"code":512})") : QByteArray(R"({"code":200})")});
    plans.append(Plan{"", 200, -1});
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn),
        done(&f, &BackendFacade::collectionCompleted), music(&f, &BackendFacade::musicRequested),
        play(&f, &BackendFacade::playbackRequested),
        report(&f, &BackendFacade::listeningReportRequested);
    connect(&f, &BackendFacade::collectionCompleted, this,
            [&](quint64, SessionToken, CollectionResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    QVERIFY(!f.mutateCollection({QUuid::createUuid(), SongHeart{{"1"}, true}}));
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.beginLogin());
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll());
    QTRY_COMPARE(login.size(), 1);
    const auto token = f.snapshot().token;
    const QList<CollectionChange> changes{
        SongHeart{{"1"}, true},
        SongHeart{{"1"}, false},
        PlaylistCollection{{"2"}, {{"1"}, {"3"}}, CollectionAction::Collect},
        PlaylistCollection{{"2"}, {{"1"}}, CollectionAction::Remove},
        AlbumCollection{{"4"}, CollectionAction::Collect},
        AlbumCollection{{"4"}, CollectionAction::Remove},
        ArtistCollection{{"5"}, CollectionAction::Collect},
        ArtistCollection{{"5"}, CollectionAction::Remove},
        PodcastSubscription{{"6"}, CollectionAction::Collect},
        PodcastSubscription{{"6"}, CollectionAction::Remove}};
    for (int i = 0; i < changes.size(); ++i) {
        const auto event = QUuid::createUuid();
        const auto id = f.mutateCollection({event, changes[i]});
        QVERIFY(id);
        QTRY_COMPARE(done.size(), i + 1);
        const auto r = qvariant_cast<CollectionResult>(done.last()[2]);
        QCOMPARE(done.last()[0].toULongLong(), id);
        QCOMPARE(r.eventId, event);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QCOMPARE(r.outcome, i == 2 ? CollectionOutcome::Rejected : CollectionOutcome::Acknowledged);
        if (i == 2)
            QCOMPARE(r.providerCode, std::optional<int>(512));
        QCOMPARE(state->requests.load(), i + 4); // one explicit write, no duplicate-ID 512 retry
    }
    QVERIFY(runtime.listening().queue().entries.isEmpty());
    QVERIFY(music.isEmpty());
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
    const auto pending = f.mutateCollection({QUuid::createUuid(), PodcastSubscription{{"6"}}});
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 14);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 11);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    QCOMPARE(qvariant_cast<CollectionResult>(done.last()[2]).outcome, CollectionOutcome::Unknown);
    QCOMPARE(qvariant_cast<CollectionResult>(done.last()[2]).error, CollectionError::Canceled);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
    QVERIFY(music.isEmpty());
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
}

void BackendRuntimeTest::accountLibraryThroughThread()
{
    QTemporaryDir directory;
    auto state = std::make_shared<SharedState>();
    Gate gate;
    const BackendConfig config{directory.filePath("state.sqlite"), "synthetic"};
    seed(config.databasePath, *state);
    auto plans = loginPlans();
    plans.append(QList<Plan>{
        Plan{
            R"({"code":200,"uid":101,"djRadios":[{"id":3,"name":"synthetic","programCount":2}],"count":5,"hasMore":true})"},
        Plan{
            R"({"code":200,"uid":101,"data":[{"songId":4,"simpleSong":{"id":99},"fileName":"synthetic.mp3"}],"hasMore":true,"count":5,"size":"0","maxSize":"1048"})"},
        Plan{R"({"code":200,"uid":101,"data":[{"songId":4,"simpleSong":{"id":99}}]})"},
        Plan{"", 200, -1}});
    BackendRuntime runtime(config, factories(state, gate, plans));
    auto& f = runtime.facade();
    QSignalSpy started(&f, &BackendFacade::started), stopped(&f, &BackendFacade::stopped),
        challenge(&f, &BackendFacade::challengeReady), login(&f, &BackendFacade::signedIn),
        done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
        report(&f, &BackendFacade::listeningReportRequested);
    connect(&f, &BackendFacade::musicCompleted, this,
            [&](quint64, domain::SessionToken, MusicResult) {
                QCOMPARE(QThread::currentThread(), qApp->thread());
            });
    f.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(f.snapshot().token.isValid());
    QVERIFY(!f.subscribedPodcasts());
    QVERIFY(!f.cloudSongs());
    QVERIFY(!f.cloudSongDetails({{"4"}}));
    QCOMPARE(state->requests.load(), 0);
    QVERIFY(f.beginLogin());
    QTRY_COMPARE(challenge.size(), 1);
    QVERIFY(f.poll());
    QTRY_COMPARE(login.size(), 1);
    const auto token = f.snapshot().token;
    QVERIFY(f.subscribedPodcasts({2, 0}));
    QTRY_COMPARE(done.size(), 1);
    const auto subscribed = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::SubscribedPodcastsPage>(subscribed));
    const auto pods = std::get<domain::SubscribedPodcastsPage>(subscribed);
    QCOMPARE(pods.query.accountId, "101");
    QCOMPARE(pods.podcasts.first().id.value, "3");
    QCOMPARE(pods.hasMore, std::optional<bool>(true));
    QVERIFY(f.cloudSongs({2, 0}));
    QTRY_COMPARE(done.size(), 2);
    const auto cloud = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CloudSongsPage>(cloud));
    const auto page = std::get<domain::CloudSongsPage>(cloud);
    QCOMPARE(page.query.accountId, "101");
    QCOMPARE(page.providerUsedSpace, std::optional<qint64>(0));
    const auto cloudId = page.songs.first().id;
    QCOMPARE(cloudId.value, "4");
    QCOMPARE(page.songs.first().catalogReference->value, "99");
    QVERIFY(f.cloudSongDetails({cloudId, {"6"}}));
    QTRY_COMPARE(done.size(), 3);
    const auto details = qvariant_cast<MusicResult>(done.last()[2]);
    QVERIFY(std::holds_alternative<domain::CloudSongDetails>(details));
    const auto result = std::get<domain::CloudSongDetails>(details);
    QCOMPARE(result.query.accountId, "101");
    QCOMPARE(result.songs.first().id.value, "4");
    QCOMPARE(result.missing.first().value, "6");
    QCOMPARE(state->requests.load(), 6); // three synthetic login requests, three explicit reads
    QVERIFY(runtime.listening().queue().entries.isEmpty());
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
    for (const auto& row : done)
        QCOMPARE(qvariant_cast<domain::SessionToken>(row[1]), token);
    const auto pending = f.cloudSongs({2, 2});
    QVERIFY(pending);
    QTRY_COMPARE(state->requests.load(), 7);
    f.close();
    QTRY_COMPARE(stopped.size(), 1);
    QTRY_COMPARE(done.size(), 4);
    QCOMPARE(done.last()[0].toULongLong(), pending);
    QCOMPARE(qvariant_cast<domain::SessionToken>(done.last()[1]), token);
    QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(done.last()[2])).error,
             MusicError::Canceled);
    QCOMPARE(state->wrongThread.load(), 0);
    QCOMPARE(state->created.load(), state->destroyed.load());
    QVERIFY(play.isEmpty());
    QVERIFY(report.isEmpty());
    QVERIFY(runtime.listening().queue().entries.isEmpty());
}

QTEST_GUILESS_MAIN(BackendRuntimeTest)
#include "backend_runtime_test.moc"
