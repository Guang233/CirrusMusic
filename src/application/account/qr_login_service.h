#pragma once

#include "application/ports/qr_login_gateway.h"
#include "session_coordinator.h"

#include <QPointer>

namespace cirrus::application
{

// Nonblocking authentication transaction in one backend event-loop thread.
// SessionCoordinator must outlive this service. UI consumes queued value signals,
// never calls this service or its gateway across threads directly.
class QrLoginService final : public QObject
{
    Q_OBJECT
  public:
    QrLoginService(SessionCoordinator& session, IQrLoginGateway& gateway,
                   QObject* parent = nullptr);
    ~QrLoginService() override;
    [[nodiscard]] domain::SessionToken begin();
    [[nodiscard]] bool poll(); // caller-paced; no automatic retry or polling timer
    void cancel();

  signals:
    void challengeReady(cirrus::domain::SessionToken context,
                        cirrus::domain::QrLoginChallenge challenge);
    void statusChanged(cirrus::domain::SessionToken context, cirrus::domain::QrLoginStatus status);
    // Failure is reported on the resulting Guest token, not an already-dead attempt.
    void failed(cirrus::domain::SessionToken context, cirrus::application::AuthFailure failure);
    void signedIn(cirrus::domain::SessionToken context, cirrus::domain::AccountIdentity identity);

  private:
    void sessionChanged(domain::SessionSnapshot snapshot);
    void cancelRequests();
    void fail(domain::SessionToken context, AuthFailure failure);
    void keyCompleted(quint64 id, domain::SessionToken context, QrKeyResult result);
    void statusCompleted(quint64 id, domain::SessionToken context, QrStatusResult result);
    void identityCompleted(quint64 id, domain::SessionToken context, IdentityResult result);
    void promotionCompleted(quint64 id, domain::SessionToken context, PromotionResult result);

    SessionCoordinator& m_session;
    QPointer<IQrLoginGateway> m_gateway;
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
