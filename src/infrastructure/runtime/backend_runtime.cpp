#include "backend_runtime.h"
#include "application/music/personal_fm_source.h"

#include "application/music/music_query_service.h"
#include "application/playback/listening_session.h"
#include "application/playback/playback_coordinator.h"
#include "infrastructure/bootstrap/anonymous_bootstrap.h"
#include "infrastructure/media/qt_playback_backend.h"
#include "infrastructure/netease/auth/account_credential_vault.h"
#include "infrastructure/netease/auth/qr_login_gateway.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include "infrastructure/netease/music/comment_write_gateway.h"
#include "infrastructure/netease/music/listening_report_gateway.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/stream_resolver.h"
#include "infrastructure/netease/music/together_gateway.h"
#include "infrastructure/secrets/tracked_secret_store.h"
#include "infrastructure/storage/installation_store.h"

#include <QDir>
#include <QThread>
#include <QTimer>

#include <atomic>

namespace cirrus::runtime
{
using namespace application;
using Kind = BackendEvent::Kind;

// Only this empty shell moves threads. All thread-affine members are constructed
// by start()/assemble(), already executing inside the backend event loop.
class BackendWorker final : public QObject
{
    Q_OBJECT
  public:
    BackendWorker(BackendConfig config, BackendFactories factories)
        : m_config(std::move(config)), m_factories(std::move(factories))
    {
    }

    void start()
    {
        if (m_started || m_closing)
            return;
        m_started = true;
        if (!QDir::isAbsolutePath(m_config.databasePath) ||
            m_config.keychainService.trimmed().isEmpty()) {
            startup(StartupError::Configuration);
            return;
        }
        m_database = std::make_unique<storage::DatabaseWorker>(
            m_config.databasePath, storage::InstallationStore::migrations());
        connect(m_database.get(), &storage::DatabaseWorker::opened, this,
                [this](storage::StorageError error) {
                    if (m_closing)
                        return;
                    if (error != storage::StorageError::None) {
                        startup(StartupError::Storage);
                        return;
                    }
                    m_installation = std::make_unique<storage::InstallationStore>(*m_database);
                    m_installation->deviceId(this, [this](storage::StorageResult<QString> result) {
                        if (m_closing)
                            return;
                        if (!result.ok())
                            startup(StartupError::Storage);
                        else
                            assemble(result.value);
                    });
                });
        m_database->open();
    }

    void command(BackendCommand command)
    {
        bool accepted = m_ready && !m_closing && m_service;
        if (accepted) {
            m_generation = command.generation;
            if (command.action != AccountAction::Poll && m_playback)
                m_playback->stop();
            switch (command.action) {
            case AccountAction::BeginLogin:
                m_service->setOptions(command.options);
                static_cast<void>(m_service->begin());
                break;
            case AccountAction::Restore:
                static_cast<void>(m_service->restore());
                break;
            case AccountAction::Poll:
                accepted = m_service->poll();
                break;
            case AccountAction::Cancel:
                m_service->cancel();
                break;
            case AccountAction::SignOut:
                m_service->signOut();
                break;
            }
            // Also acknowledge no-op cancel/signout with an authoritative snapshot.
            publishSession(m_session->snapshot());
        }
        auto event = makeEvent(Kind::Acknowledged);
        event.commandId = command.id;
        event.accepted = accepted;
        emit delivered(event);
    }

    void music(MusicCommand command)
    {
        const auto id = m_ready && !m_closing && m_musicService
                            ? m_musicService->request(command.context, command.query)
                            : 0;
        if (id == 0)
            emit musicDelivered(
                {command.id, command.generation, command.context, MusicFailure{MusicError::Busy}});
        else
            m_musicRequests.insert(id, std::move(command));
    }

    void cancelMusic(quint64 requestId)
    {
        if (!m_musicService)
            return;
        for (auto it = m_musicRequests.cbegin(); it != m_musicRequests.cend(); ++it) {
            if (it->id == requestId) {
                m_musicService->cancel(it.key());
                return;
            }
        }
    }

    void reportListening(ListeningReportCommand command)
    {
        const auto id = m_ready && !m_closing && m_reports && command.generation == m_generation
                            ? m_reports->submit(command.context, command.accountId, command.report)
                            : 0;
        if (!id)
            emit reportDelivered({command.id,
                                  command.generation,
                                  command.context,
                                  {command.report.eventId, command.report.action,
                                   domain::ReportOutcome::NotSent, domain::ReportError::Busy}});
        else
            m_reportRequests.insert(id, command);
    }
    void cancelReport(quint64 id)
    {
        if (!m_reports)
            return;
        for (auto it = m_reportRequests.cbegin(); it != m_reportRequests.cend(); ++it)
            if (it->id == id) {
                m_reports->cancel(it.key());
                return;
            }
    }

    void mutateComment(CommentWriteCommand c)
    {
        const auto id = m_ready && !m_closing && m_commentWrites && c.generation == m_generation
                            ? m_commentWrites->submit(c.context, c.accountId, c.mutation)
                            : 0;
        if (!id)
            emit commentWriteDelivered({c.id,
                                        c.generation,
                                        c.context,
                                        {c.mutation.eventId, domain::CommentWriteOutcome::NotSent,
                                         domain::CommentWriteError::Busy}});
        else
            m_commentWriteRequests.insert(id, std::move(c));
    }
    void cancelComment(quint64 id)
    {
        if (!m_commentWrites)
            return;
        for (auto it = m_commentWriteRequests.cbegin(); it != m_commentWriteRequests.cend(); ++it)
            if (it->id == id) {
                m_commentWrites->cancel(it.key());
                return;
            }
    }

    void together(TogetherCommand c)
    {
        const auto id = m_ready && !m_closing && m_together && c.generation == m_generation
                            ? m_together->submit(c.context, c.accountId, c.request)
                            : 0;
        if (!id) {
            domain::TogetherResult r{
                c.request.eventId, domain::TogetherOutcome::NotSent, domain::TogetherError::Busy,
                domain::togetherKind(c.request.action), c.request.roomGeneration};
            emit togetherDelivered({c.id, c.generation, c.context, r});
        } else
            m_togetherRequests.insert(id, std::move(c));
    }
    void cancelTogether(quint64 id)
    {
        if (!m_together)
            return;
        for (auto it = m_togetherRequests.cbegin(); it != m_togetherRequests.cend(); ++it)
            if (it->id == id) {
                m_together->cancel(it.key());
                return;
            }
    }

    void mutateCollection(CollectionCommand command)
    {
        const auto id =
            m_ready && !m_closing && m_collections && command.generation == m_generation
                ? m_collections->submit(command.context, command.accountId, command.mutation)
                : 0;
        if (!id)
            emit collectionDelivered({command.id,
                                      command.generation,
                                      command.context,
                                      {command.mutation.eventId, domain::CollectionOutcome::NotSent,
                                       domain::CollectionError::Busy}});
        else
            m_collectionRequests.insert(id, command);
    }
    void cancelCollection(quint64 id)
    {
        if (!m_collections)
            return;
        for (auto it = m_collectionRequests.cbegin(); it != m_collectionRequests.cend(); ++it)
            if (it->id == id) {
                m_collections->cancel(it.key());
                return;
            }
    }

    void playback(PlaybackCommand command)
    {
        bool accepted = m_ready && !m_closing && m_playback && command.generation == m_generation &&
                        m_session->accepts(command.token.session);
        if (accepted) {
            if (command.action == PlaybackAction::Play || command.action == PlaybackAction::Stop) {
                m_playbackToken = command.token;
                m_playbackParentGeneration = command.generation;
                if (command.action == PlaybackAction::Play)
                    accepted = m_playback->start(command.token, command.query, command.intent);
                else
                    m_playback->stop(command.token);
            } else if (command.action == PlaybackAction::Volume)
                m_playback->setVolume(command.volume);
            else if (command.token != m_playbackToken)
                accepted = false;
            else if (command.action == PlaybackAction::Pause)
                accepted = m_playback->pause();
            else if (command.action == PlaybackAction::Resume)
                accepted = m_playback->resume();
            else if (command.action == PlaybackAction::Seek)
                accepted = m_playback->seek(command.positionMs);
        }
        auto event = makeEvent(Kind::Acknowledged);
        event.commandId = command.id;
        event.accepted = accepted;
        emit delivered(event);
    }

    void close()
    {
        if (m_closing)
            return;
        m_closing = true;
        m_ready = false;
        // Stop producers, not stored identity. In particular, do NOT call signOut
        // or anonymous.reset on normal application exit.
        m_anonymous.reset();
        m_registrar.reset();
        m_playback.reset();
        m_streamResolver.reset();
        m_player.reset();
        m_reports.reset();
        m_reportRequests.clear();
        m_collections.reset();
        m_collectionRequests.clear();
        m_commentWrites.reset();
        m_commentWriteRequests.clear();
        m_together.reset();
        m_togetherRequests.clear();
        m_musicService.reset();
        m_musicGateway.reset();
        m_musicRequests.clear();
        m_service.reset();
        m_gateway.reset();
        queueDrain();
    }

  signals:
    void delivered(cirrus::application::BackendEvent event);
    void drained(bool persistenceFailed);
    void musicDelivered(cirrus::application::MusicDelivery delivery);
    void playbackDelivered(cirrus::application::PlaybackDelivery delivery);
    void reportDelivered(cirrus::application::ListeningReportDelivery delivery);
    void collectionDelivered(cirrus::application::CollectionDelivery delivery);
    void commentWriteDelivered(cirrus::application::CommentWriteDelivery delivery);
    void togetherDelivered(cirrus::application::TogetherDelivery delivery);

  private:
    BackendEvent makeEvent(Kind kind) const
    {
        BackendEvent event;
        event.kind = kind;
        event.generation = m_generation;
        if (m_session)
            event.session = m_session->snapshot();
        return event;
    }

    void publishSession(domain::SessionSnapshot snapshot)
    {
        auto event = makeEvent(Kind::Session);
        event.session = std::move(snapshot);
        emit delivered(event);
    }

    void startup(StartupError error)
    {
        if (m_closing)
            return;
        m_ready = error == StartupError::None || error == StartupError::GuestBootstrap;
        auto event = makeEvent(Kind::Started);
        event.startupError = error;
        emit delivered(event);
        if (m_session)
            publishSession(m_session->snapshot());
    }

    void assemble(const QString& deviceId)
    {
        auto store = m_factories.secrets
                         ? m_factories.secrets()
                         : std::make_unique<secrets::KeychainSecretStore>(m_config.keychainService);
        if (!store) {
            startup(StartupError::Configuration);
            return;
        }
        m_store = std::make_unique<secrets::TrackedSecretStore>(std::move(store));
        connect(m_store.get(), &secrets::TrackedSecretStore::idle, this,
                &BackendWorker::queueDrain);
        m_session = std::make_unique<SessionCoordinator>();
        m_ring = std::make_unique<netease::XeapiKeyRing>();
        auto apiTransport = m_factories.apiTransport ? m_factories.apiTransport()
                                                     : std::make_unique<netease::HttpTransport>();
        auto keyTransport = m_factories.keyTransport ? m_factories.keyTransport()
                                                     : std::make_unique<netease::HttpTransport>();
        if (!apiTransport || !keyTransport) {
            startup(StartupError::Configuration);
            return;
        }
        m_client = std::make_unique<netease::SessionApiClient>(std::move(apiTransport), deviceId);
        m_client->setXeapiKeyRing(m_ring.get());
        m_keyVault = std::make_unique<bootstrap::XeapiKeyVault>(*m_ring, *m_store);
        connect(m_keyVault.get(), &bootstrap::XeapiKeyVault::persistFailed, this,
                [this](secrets::SecretError) {
                    auto event = makeEvent(Kind::StorageWarning);
                    event.persistenceArea = PersistenceArea::ProtocolKey;
                    emit delivered(event);
                });
        m_registrar = std::make_unique<netease::XeapiKeyRegistrar>(*m_ring, deviceId,
                                                                   std::move(keyTransport));
        m_accountVault = std::make_unique<netease::AccountCredentialVault>(*m_client, *m_store);
        connect(m_accountVault.get(), &netease::AccountCredentialVault::idle, this,
                &BackendWorker::queueDrain);
        // Independent of gateway lifetime: logout/save outcomes still reach the
        // caller while closing; shutdown additionally reports any write failure.
        connect(m_accountVault.get(), &netease::AccountCredentialVault::settled, this,
                [this](domain::SessionToken token, bool remembered, secrets::SecretError error) {
                    auto event = makeEvent(Kind::Saved);
                    event.session.token = token;
                    event.saved =
                        error == secrets::SecretError::None
                            ? SavedSessionResult(remembered)
                            : SavedSessionResult(AuthFailure{AuthError::CredentialRejected});
                    emit delivered(event);
                });
        m_gateway = std::make_unique<netease::QrLoginGateway>(*m_client, *m_accountVault);
        m_service = std::make_unique<QrLoginService>(*m_session, *m_gateway);
        m_musicGateway = std::make_unique<netease::MusicGateway>(*m_client);
        m_musicService = std::make_unique<MusicQueryService>(*m_session, *m_musicGateway);
        m_reports = std::make_unique<netease::ListeningReportGateway>(*m_session, *m_client);
        connect(
            m_reports.get(), &IListeningReportGateway::completed, this,
            [this](quint64 id, domain::SessionToken context, domain::ListeningReportResult result) {
                if (!m_reportRequests.contains(id))
                    return;
                const auto c = m_reportRequests.take(id);
                emit reportDelivered({c.id, c.generation, context, result});
            });
        auto coverUpload = m_factories.coverUpload ? m_factories.coverUpload()
                                                   : std::make_unique<netease::CoverUpload>();
        if (!coverUpload) {
            startup(StartupError::Configuration);
            return;
        }
        m_collections = std::make_unique<netease::CollectionGateway>(*m_session, *m_client,
                                                                     std::move(coverUpload));
        connect(m_collections.get(), &ICollectionGateway::completed, this,
                [this](quint64 id, domain::SessionToken context, domain::CollectionResult result) {
                    if (!m_collectionRequests.contains(id))
                        return;
                    const auto c = m_collectionRequests.take(id);
                    emit collectionDelivered({c.id, c.generation, context, result});
                });
        auto issuer = m_factories.commentV3Issuer ? m_factories.commentV3Issuer()
                                                  : std::make_unique<netease::CommentV3Issuer>();
        if (!issuer) {
            startup(StartupError::Configuration);
            return;
        }
        m_commentWrites = std::make_unique<netease::CommentWriteGateway>(*m_session, *m_client,
                                                                         std::move(issuer));
        connect(m_commentWrites.get(), &ICommentWriteGateway::completed, this,
                [this](quint64 id, domain::SessionToken token, domain::CommentWriteResult result) {
                    if (!m_commentWriteRequests.contains(id))
                        return;
                    const auto c = m_commentWriteRequests.take(id);
                    emit commentWriteDelivered({c.id, c.generation, token, result});
                });
        m_together = std::make_unique<netease::TogetherGateway>(*m_session, *m_client);
        connect(m_together.get(), &ITogetherGateway::completed, this,
                [this](quint64 id, domain::SessionToken context, domain::TogetherResult result) {
                    if (!m_togetherRequests.contains(id))
                        return;
                    const auto c = m_togetherRequests.take(id);
                    emit togetherDelivered({c.id, c.generation, context, std::move(result)});
                });
        m_player = m_factories.playback ? m_factories.playback()
                                        : std::make_unique<media::QtPlaybackBackend>();
        if (!m_player) {
            startup(StartupError::Configuration);
            return;
        }
        m_streamResolver = std::make_unique<netease::StreamResolver>(*m_client);
        m_playback =
            std::make_unique<PlaybackCoordinator>(*m_session, *m_streamResolver, *m_player);
        connect(m_playback.get(), &PlaybackCoordinator::changed, this,
                [this](domain::PlaybackSnapshot snapshot) {
                    if (snapshot.token == m_playbackToken)
                        emit playbackDelivered({m_playbackParentGeneration, std::move(snapshot)});
                });
        connect(m_musicService.get(), &MusicQueryService::completed, this,
                [this](quint64 id, domain::SessionToken context, MusicResult result) {
                    if (!m_musicRequests.contains(id))
                        return;
                    const auto command = m_musicRequests.take(id);
                    emit musicDelivered(
                        {command.id, command.generation, context, std::move(result)});
                });
        connect(m_session.get(), &SessionCoordinator::changed, this,
                &BackendWorker::publishSession);
        connect(m_service.get(), &QrLoginService::challengeReady, this,
                [this](domain::SessionToken token, domain::QrLoginChallenge challenge) {
                    auto event = makeEvent(Kind::Challenge);
                    event.session.token = token;
                    event.challenge = std::move(challenge);
                    emit delivered(event);
                });
        connect(m_service.get(), &QrLoginService::statusChanged, this,
                [this](domain::SessionToken token, domain::QrLoginStatus status) {
                    auto event = makeEvent(Kind::Status);
                    event.session.token = token;
                    event.status = status;
                    emit delivered(event);
                });
        connect(m_service.get(), &QrLoginService::signedIn, this,
                [this](domain::SessionToken token, domain::AccountIdentity identity) {
                    auto event = makeEvent(Kind::SignedIn);
                    event.session.token = token;
                    event.identity = std::move(identity);
                    emit delivered(event);
                });
        connect(m_service.get(), &QrLoginService::failed, this,
                [this](domain::SessionToken token, AuthFailure failure) {
                    auto event = makeEvent(Kind::AuthFailed);
                    event.session.token = token;
                    event.failure = failure;
                    emit delivered(event);
                });
        m_anonymous = std::make_unique<bootstrap::AnonymousBootstrap>(
            *m_client, *m_ring, *m_registrar, *m_keyVault, *m_store, deviceId,
            [this] { return m_session->snapshot().token; });
        connect(m_anonymous.get(), &bootstrap::AnonymousBootstrap::finished, this,
                [this](bootstrap::BootstrapResult result) {
                    startup(result.error == bootstrap::BootstrapError::None
                                ? StartupError::None
                                : StartupError::GuestBootstrap);
                });
        connect(m_anonymous.get(), &bootstrap::AnonymousBootstrap::persistFailed, this,
                [this](secrets::SecretError) {
                    auto event = makeEvent(Kind::StorageWarning);
                    event.persistenceArea = PersistenceArea::AnonymousToken;
                    emit delivered(event);
                });
        static_cast<void>(m_anonymous->start());
    }

    void queueDrain()
    {
        if (!m_closing || m_drainQueued)
            return;
        m_drainQueued = true;
        QTimer::singleShot(0, this, [this] {
            m_drainQueued = false;
            if ((m_store && m_store->pending() != 0) ||
                (m_accountVault && !m_accountVault->isIdle()) ||
                (m_keyVault && !m_keyVault->isIdle()))
                return;
            const bool failed = m_store && m_store->writeFailed();
            m_accountVault.reset();
            m_keyVault.reset();
            m_client.reset();
            m_ring.reset();
            m_session.reset();
            m_store.reset();
            m_installation.reset();
            // DatabaseWorker joins its own SQL executor here, NOT on the UI thread.
            // Queued DB jobs settle and the process lock is released before stopped.
            m_database.reset();
            emit drained(failed);
        });
    }

    BackendConfig m_config;
    BackendFactories m_factories;
    bool m_started = false;
    bool m_ready = false;
    bool m_closing = false;
    bool m_drainQueued = false;
    quint64 m_generation = 0;
    std::unique_ptr<storage::DatabaseWorker> m_database;
    std::unique_ptr<storage::InstallationStore> m_installation;
    std::unique_ptr<secrets::TrackedSecretStore> m_store;
    std::unique_ptr<SessionCoordinator> m_session;
    std::unique_ptr<netease::XeapiKeyRing> m_ring;
    std::unique_ptr<netease::SessionApiClient> m_client;
    std::unique_ptr<bootstrap::XeapiKeyVault> m_keyVault;
    std::unique_ptr<netease::XeapiKeyRegistrar> m_registrar;
    std::unique_ptr<netease::AccountCredentialVault> m_accountVault;
    std::unique_ptr<netease::QrLoginGateway> m_gateway;
    std::unique_ptr<QrLoginService> m_service;
    std::unique_ptr<bootstrap::AnonymousBootstrap> m_anonymous;
    std::unique_ptr<netease::MusicGateway> m_musicGateway;
    std::unique_ptr<MusicQueryService> m_musicService;
    QHash<quint64, MusicCommand> m_musicRequests;
    std::unique_ptr<netease::ListeningReportGateway> m_reports;
    QHash<quint64, ListeningReportCommand> m_reportRequests;
    std::unique_ptr<netease::CollectionGateway> m_collections;
    QHash<quint64, CollectionCommand> m_collectionRequests;
    std::unique_ptr<netease::CommentWriteGateway> m_commentWrites;
    QHash<quint64, CommentWriteCommand> m_commentWriteRequests;
    std::unique_ptr<netease::TogetherGateway> m_together;
    QHash<quint64, TogetherCommand> m_togetherRequests;
    std::unique_ptr<IPlaybackBackend> m_player;
    std::unique_ptr<netease::StreamResolver> m_streamResolver;
    std::unique_ptr<PlaybackCoordinator> m_playback;
    domain::LoadToken m_playbackToken;
    quint64 m_playbackParentGeneration = 0;
};

BackendRuntime::BackendRuntime(BackendConfig config, BackendFactories factories, QObject* parent)
    : QObject(parent), m_facade(new BackendFacade(this)),
      m_fm(new PersonalFmSource(*m_facade, this)),
      m_listening(new ListeningSession(*m_facade, *m_fm, this))
{
    auto* thread = new QThread; // self-deletes after finish; never destroyed while running
    thread->setObjectName(QStringLiteral("CirrusBackend"));
    auto* worker = new BackendWorker(std::move(config), std::move(factories));
    worker->moveToThread(thread);
    auto outcome = std::make_shared<std::atomic_bool>(false);
    connect(m_facade, &BackendFacade::startRequested, worker, &BackendWorker::start,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::commandRequested, worker, &BackendWorker::command,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::closeRequested, worker, &BackendWorker::close,
            Qt::QueuedConnection);
    connect(worker, &BackendWorker::delivered, m_facade, &BackendFacade::deliver,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::musicRequested, worker, &BackendWorker::music,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::musicCancelRequested, worker, &BackendWorker::cancelMusic,
            Qt::QueuedConnection);
    connect(worker, &BackendWorker::musicDelivered, m_facade, &BackendFacade::deliverMusic,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::listeningReportRequested, worker,
            &BackendWorker::reportListening, Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::listeningReportCancelRequested, worker,
            &BackendWorker::cancelReport, Qt::QueuedConnection);
    connect(worker, &BackendWorker::reportDelivered, m_facade,
            &BackendFacade::deliverListeningReport, Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::commentWriteRequested, worker, &BackendWorker::mutateComment,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::commentWriteCancelRequested, worker,
            &BackendWorker::cancelComment, Qt::QueuedConnection);
    connect(worker, &BackendWorker::commentWriteDelivered, m_facade,
            &BackendFacade::deliverCommentWrite, Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::togetherRequested, worker, &BackendWorker::together,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::togetherCancelRequested, worker,
            &BackendWorker::cancelTogether, Qt::QueuedConnection);
    connect(worker, &BackendWorker::togetherDelivered, m_facade, &BackendFacade::deliverTogether,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::collectionRequested, worker, &BackendWorker::mutateCollection,
            Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::collectionCancelRequested, worker,
            &BackendWorker::cancelCollection, Qt::QueuedConnection);
    connect(worker, &BackendWorker::collectionDelivered, m_facade,
            &BackendFacade::deliverCollection, Qt::QueuedConnection);
    connect(m_facade, &BackendFacade::playbackRequested, worker, &BackendWorker::playback,
            Qt::QueuedConnection);
    connect(worker, &BackendWorker::playbackDelivered, m_facade, &BackendFacade::deliverPlayback,
            Qt::QueuedConnection);
    connect(
        worker, &BackendWorker::drained, thread,
        [thread, outcome](bool failed) {
            outcome->store(failed);
            thread->quit(); // quit() is explicitly thread-safe
        },
        Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(
        thread, &QThread::finished, m_facade,
        [facade = m_facade, outcome] { facade->finishClose(outcome->load()); },
        Qt::QueuedConnection);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(); // idle event loop only; no IO until explicit facade.start()
}

BackendRuntime::~BackendRuntime()
{
    delete m_listening; // injected source and facade must outlive the listening session
    m_listening = nullptr;
    delete m_fm;
    m_fm = nullptr;
    m_facade->close();
}

} // namespace cirrus::runtime

#include "backend_runtime.moc"
