#pragma once

#include "application/ports/qr_login_gateway.h"
#include "login_scheduler.h"
#include "session_coordinator.h"

#include <QPointer>

#include <chrono>
#include <memory>

namespace cirrus::application
{

using Milliseconds = std::chrono::milliseconds;

// Conservative engineering defaults, NOT official NetEase rates or QR lifetimes.
// Snapshotted at begin(); later changes apply to the next attempt only.
struct QrLoginOptions {
    static constexpr Milliseconds MinPollInterval = Milliseconds(1000);
    static constexpr Milliseconds MaxPollInterval = Milliseconds(60000);
    static constexpr Milliseconds MaxTotalTimeout = Milliseconds(600000);

    bool autoPoll = false; // explicit opt-in; manual poll() stays available
    Milliseconds pollInterval = Milliseconds(3000);
    // Whole attempt, including identity verification and Committing.
    Milliseconds totalTimeout = Milliseconds(180000);
    bool rememberSession = false; // explicit consent; otherwise memory-only

    [[nodiscard]] bool isValid() const
    {
        return pollInterval >= MinPollInterval && pollInterval <= MaxPollInterval &&
               totalTimeout >= pollInterval && totalTimeout <= MaxTotalTimeout;
    }
};

// Nonblocking authentication transaction in one backend event-loop thread.
// SessionCoordinator (and an injected scheduler) must outlive this service. UI
// consumes queued value signals, never calls this service or its gateway across
// threads directly. Do not destroy the service synchronously from its signals.
class QrLoginService final : public QObject
{
    Q_OBJECT
  public:
    QrLoginService(SessionCoordinator& session, IQrLoginGateway& gateway,
                   QObject* parent = nullptr);
    // scheduler == nullptr uses an internal Qt Core monotonic scheduler.
    QrLoginService(SessionCoordinator& session, IQrLoginGateway& gateway, QrLoginOptions options,
                   ILoginScheduler* scheduler = nullptr, QObject* parent = nullptr);
    ~QrLoginService() override;

    [[nodiscard]] QrLoginOptions options() const { return m_options; }
    void setOptions(QrLoginOptions options) { m_options = options; } // next begin()

    [[nodiscard]] domain::SessionToken begin();
    [[nodiscard]] domain::SessionToken
    restore();      // saved credentials still require identity verification
    void signOut(); // local invalidation + removal of the remembered account
    // Manual mode: caller-paced. Auto mode: also refused (false, no signal) before
    // the next allowed time. Never more than one remote request; no error retry.
    [[nodiscard]] bool poll();
    void cancel();

  signals:
    void challengeReady(cirrus::domain::SessionToken context,
                        cirrus::domain::QrLoginChallenge challenge);
    void statusChanged(cirrus::domain::SessionToken context, cirrus::domain::QrLoginStatus status);
    // Failure is reported on the resulting Guest token, not an already-dead attempt.
    void failed(cirrus::domain::SessionToken context, cirrus::application::AuthFailure failure);
    void signedIn(cirrus::domain::SessionToken context, cirrus::domain::AccountIdentity identity);
    void savedSessionCompleted(cirrus::domain::SessionToken context,
                               cirrus::application::SavedSessionResult result);

  private:
    // Local schedule of one authentication attempt (Authenticating -> Committing).
    struct Schedule {
        quint64 generation = 0; // 0: no active attempt
        bool autoPoll = false;
        bool rememberSession = false;
        Milliseconds interval = Milliseconds(0);
        Milliseconds deadline = Milliseconds(0); // absolute monotonic
        Milliseconds nextPollAt = Milliseconds(0);
        quint64 deadlineTimer = 0;
        quint64 pollTimer = 0;
    };

    [[nodiscard]] domain::SessionToken startAttempt(bool restore);
    void sessionChanged(domain::SessionSnapshot snapshot);
    void cancelRequests();
    void endSchedule();
    void armDeadline();
    void deadlineReached(quint64 generation);
    void schedulePoll();
    void pollDue(quint64 generation);
    void stopPollTimer();
    void sendPoll();
    [[nodiscard]] bool pastDeadline() const;
    [[nodiscard]] bool failIfTimedOut();
    void fail(domain::SessionToken context, AuthFailure failure);
    void keyCompleted(quint64 id, domain::SessionToken context, QrKeyResult result);
    void statusCompleted(quint64 id, domain::SessionToken context, QrStatusResult result);
    void identityCompleted(quint64 id, domain::SessionToken context, IdentityResult result);
    void promotionCompleted(quint64 id, domain::SessionToken context, PromotionResult result);

    SessionCoordinator& m_session;
    QPointer<IQrLoginGateway> m_gateway;
    std::unique_ptr<QtLoginScheduler> m_ownedScheduler;
    ILoginScheduler* m_scheduler = nullptr;
    QrLoginOptions m_options;
    Schedule m_schedule;
    quint64 m_generations = 0;
    bool m_preparing = false;
    domain::SessionPhase m_phase = domain::SessionPhase::Guest;
    domain::SessionToken m_context;
    domain::SessionToken m_synchronized;
    QString m_key;
    QString m_expectedAccount;
    quint64 m_keyRequest = 0;
    quint64 m_pollRequest = 0;
    quint64 m_identityRequest = 0;
    quint64 m_promotionRequest = 0;
};

} // namespace cirrus::application
