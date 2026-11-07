#pragma once

#include "sources.h"

#include "domain/account/session_types.h"

#include <QObject>
#include <QPointer>

namespace cirrus::application
{
class BackendFacade;
}
namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

// The account from the backend (docs/ui/integration.md §4 SessionSource): QR sign-in
// with the code drawn here (Nayuki, D2), phone/email sign-in and SMS codes
// (docs/api/credential-login.md), and restoring a remembered account once the backend
// is ready. Translates backend failures into short messages for the dialog.
class BackendSession : public QObject, public SessionSource
{
  public:
    explicit BackendSession(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendSession() override;

    [[nodiscard]] const SessionSnapshot& snapshot() const override { return m_state; }
    void setChangedHandler(std::function<void()> changed) override;

    void beginQrLogin(bool remember) override;
    void refreshQr() override;
    void cancelLogin() override;
    void signOut() override;
    void beginFormLogin(account::FormMethod method, const QString& countryCode, const QString& user,
                        const QString& secret, bool remember) override;
    void sendSmsCode(const QString& countryCode, const QString& phone) override;
    void clearFormError() override;

  private:
    // What the running sign-in attempt is, so a failure lands in the right place.
    enum class Attempt { None, Restore, Qr, Form };

    void applyBackend();
    void notify();

    QPointer<application::BackendFacade> m_facade;
    SessionSnapshot m_state;
    std::function<void()> m_changed;
    Attempt m_attempt = Attempt::None;
    bool m_remember = true;
    quint64 m_formCommand = 0;
    quint64 m_smsCommand = 0;
    domain::SessionToken m_smsToken; // the session an SMS code was asked on
    bool m_notifyPending = false;
};

} // namespace cirrus::ui
