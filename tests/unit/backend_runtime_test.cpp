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

void BackendRuntimeTest::playbackThroughThread()
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
    auto f = factories(
        state, gate,
        {{*response, 200, 10}, lyric, {*response2, 200, 10}, lyric, {"", 200, -1}, {"", 200, -1}});
    f.playback = [state] { return std::make_unique<Player>(state); };
    BackendRuntime runtime(config, std::move(f));
    auto& facade = runtime.facade();
    QSignalSpy started(&facade, &BackendFacade::started), stopped(&facade, &BackendFacade::stopped);
    facade.start();
    QTRY_COMPARE(started.size(), 1);
    QTRY_VERIFY(facade.snapshot().token.isValid());
    auto& listening = runtime.listening();
    QVERIFY(listening.setVolume(0));
    QVERIFY(listening.replaceQueue({{"1"}, {"2"}, {"3"}}));
    QVERIFY(listening.pause());
    QTRY_COMPARE(facade.playback().state, domain::PlaybackState::Paused);
    QCOMPARE(facade.playback().actualQuality, "standard");
    QTRY_COMPARE(listening.lyricsState(), domain::LyricsState::Ready);
    QVERIFY(listening.seek(1250));
    QTRY_COMPARE(facade.playback().positionMs, 1250);
    QTRY_COMPARE(listening.cursors().original.standard.lines, (QList<qsizetype>{1}));
    QVERIFY(listening.next()); // atomic initial Pause, not a second command after Play
    QTRY_COMPARE(facade.playback().state, domain::PlaybackState::Paused);
    QTRY_COMPARE(listening.lyricsState(), domain::LyricsState::Ready);
    QCOMPARE(listening.lyrics()->song.value, "2");
    QVERIFY(listening.resume());
    QTRY_COMPARE(facade.playback().state, domain::PlaybackState::Playing);
    QVERIFY(listening.next());
    QTRY_COMPARE(state->requests.load(), 6);
    facade.close();
    QTRY_COMPARE(stopped.size(), 1);
    QCOMPARE(facade.playback().state, domain::PlaybackState::Empty);
    QVERIFY(listening.queue().entries.isEmpty());
    QVERIFY(!listening.lyrics());
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

QTEST_GUILESS_MAIN(BackendRuntimeTest)
#include "backend_runtime_test.moc"
