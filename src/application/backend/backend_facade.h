#pragma once

#include "application/account/qr_login_service.h"

#include <QSet>

namespace cirrus::application
{

enum class BackendState { Dormant, Starting, Ready, Failed, Stopping, Stopped };
enum class StartupError { None, Configuration, Storage, GuestBootstrap };
enum class AccountAction { BeginLogin, Restore, Poll, Cancel, SignOut };
enum class PersistenceArea { ProtocolKey, AnonymousToken };

struct BackendCommand {
    quint64 id = 0;
    quint64 generation = 0;
    AccountAction action{};
    QrLoginOptions options;
};

// A value-only, internal queued delivery envelope. No infrastructure objects or
// credentials cross threads. Generation invalidates events already queued to UI.
struct BackendEvent {
    enum class Kind {
        Started,
        Session,
        Challenge,
        Status,
        SignedIn,
        AuthFailed,
        Saved,
        Acknowledged,
        StorageWarning
    };
    Kind kind{};
    quint64 generation = 0;
    quint64 commandId = 0;
    bool accepted = false;
    StartupError startupError{};
    PersistenceArea persistenceArea{};
    domain::SessionSnapshot session;
    domain::QrLoginChallenge challenge;
    domain::QrLoginStatus status{};
    domain::AccountIdentity identity;
    AuthFailure failure;
    SavedSessionResult saved = false;
};

// Caller/UI-thread asynchronous surface. No QML dependency and no synchronous
// cross-thread reads. Runtime installs queued signal/slot wiring before use.
// One-shot start/close. Command ID 0 means rejected locally (not ready/overload).
// Other IDs receive an acknowledgement, NOT proof that authentication succeeded.
// close() cancels unobserved acknowledgements with false; that is NOT evidence
// that a previously queued command had no side effects. Observe storage results.
class BackendFacade final : public QObject
{
    Q_OBJECT
  public:
    explicit BackendFacade(QObject* parent = nullptr);
    [[nodiscard]] BackendState state() const { return m_state; }
    [[nodiscard]] domain::SessionSnapshot snapshot() const { return m_snapshot; }
    void start();
    void close(); // does NOT sign out; keep event loop alive until stopped()
    [[nodiscard]] quint64 beginLogin(QrLoginOptions options = {});
    [[nodiscard]] quint64 restore();
    [[nodiscard]] quint64 poll();
    [[nodiscard]] quint64 cancel();
    [[nodiscard]] quint64 signOut();

    // Runtime delivery endpoints; queued connections only, not public commands.
    void deliver(BackendEvent event);
    void finishClose(bool persistenceFailed);

  signals:
    void stateChanged(cirrus::application::BackendState state);
    void started(
        cirrus::application::StartupError error); // GuestBootstrap = degraded, QR still available
    void
    stopped(bool persistenceFailed); // all jobs settled; false is not live backend certification
    void closeDelayed();             // once after 5s; no force termination or false stopped result
    void sessionChanged(cirrus::domain::SessionSnapshot snapshot);
    void challengeReady(cirrus::domain::SessionToken context,
                        cirrus::domain::QrLoginChallenge challenge);
    void statusChanged(cirrus::domain::SessionToken context, cirrus::domain::QrLoginStatus status);
    void signedIn(cirrus::domain::SessionToken context, cirrus::domain::AccountIdentity identity);
    void failed(cirrus::domain::SessionToken context, cirrus::application::AuthFailure failure);
    void savedSessionCompleted(cirrus::domain::SessionToken context,
                               cirrus::application::SavedSessionResult result);
    void acknowledged(quint64 commandId, bool accepted);
    void persistenceWarning(cirrus::application::PersistenceArea area);

    // Composition wiring only; emitted on this object's thread.
    void startRequested();
    void closeRequested();
    void commandRequested(cirrus::application::BackendCommand command);

  private:
    quint64 submit(AccountAction action, QrLoginOptions options = {});
    void setState(BackendState state);
    BackendState m_state = BackendState::Dormant;
    domain::SessionSnapshot m_snapshot;
    QSet<quint64> m_pending;
    quint64 m_nextId = 0;
    quint64 m_generation = 0;
};

} // namespace cirrus::application

Q_DECLARE_METATYPE(cirrus::application::BackendState)
Q_DECLARE_METATYPE(cirrus::application::StartupError)
Q_DECLARE_METATYPE(cirrus::application::PersistenceArea)
Q_DECLARE_METATYPE(cirrus::application::BackendCommand)
Q_DECLARE_METATYPE(cirrus::application::BackendEvent)
