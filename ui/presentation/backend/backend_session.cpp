#include "backend_session.h"
#include "logging/log.h"

#include "application/backend/backend_facade.h"
#include "domain/account/credential_login_types.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <qrcodegen.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QLoggingCategory>
#include <QSettings>

namespace cirrus::ui
{
namespace
{

using application::AuthError;
using application::AuthFailure;
using application::BackendState;

// Whether an account was remembered: only then is it restored at start, so guests
// don't see "signing in" for a moment every time.
const QString kRememberedKey = QStringLiteral("account/remembered");
// How long the code button waits after a code was sent.
constexpr qint64 kResendMs = 60000;

QString tr(const char* text) { return QCoreApplication::translate("Session", text); }

// Provider codes from community sources, not verified here (credential-login.md §3).
QString failureText(const AuthFailure& failure, bool sms)
{
    if (failure.providerCode) {
        switch (*failure.providerCode) {
        case 501:
            return tr("账号不存在");
        case 502:
            return tr("密码错误");
        case 503:
            return tr("验证码错误");
        case 405:
            return tr("操作太频繁，请稍后再试");
        case 8821:
        case -462:
            return tr("网易要求安全验证，请扫码登录");
        default:
            break;
        }
    }
    switch (failure.error) {
    case AuthError::Canceled:
    case AuthError::StaleContext:
        return {};
    case AuthError::RateLimited:
    case AuthError::Busy:
        return tr("操作太频繁，请稍后再试");
    case AuthError::Network:
    case AuthError::Timeout:
        return tr("网络连接失败");
    default:
        break;
    }
    const QString what = sms ? tr("验证码发送失败") : tr("登录失败");
    return failure.providerCode ? tr("%1（%2）").arg(what).arg(*failure.providerCode) : what;
}

// The code for `url`, black on white with a two-module quiet zone; the dialog adds
// white around it.
QImage qrImage(const QUrl& url)
{
    try {
        const auto code = qrcodegen::QrCode::encodeText(url.toEncoded().constData(),
                                                        qrcodegen::QrCode::Ecc::MEDIUM);
        constexpr int border = 2;
        const int size = code.getSize();
        QImage image(size + 2 * border, size + 2 * border, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if (code.getModule(x, y))
                    image.setPixel(x + border, y + border, qRgb(0, 0, 0));
        return image;
    } catch (const qrcodegen::data_too_long&) {
        return {};
    }
}

account::QrPhase phaseOf(domain::QrLoginStatus status)
{
    switch (status) {
    case domain::QrLoginStatus::WaitingForScan:
        return account::WaitingForScan;
    case domain::QrLoginStatus::WaitingForConfirmation:
        return account::WaitingForConfirmation;
    case domain::QrLoginStatus::Authorized:
        return account::Confirming;
    case domain::QrLoginStatus::Expired:
        return account::QrExpired;
    }
    return account::NoQr;
}

} // namespace

BackendSession::BackendSession(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_facade(&runtime.facade())
{
    using application::BackendFacade;
    auto* facade = m_facade.data();
    connect(facade, &BackendFacade::stateChanged, this, [this] { applyBackend(); });
    connect(facade, &BackendFacade::started, this, [this](application::StartupError) {
        applyBackend();
        if (m_facade && m_facade->state() == BackendState::Ready &&
            QSettings().value(kRememberedKey).toBool() && m_attempt == Attempt::None) {
            if (m_facade->restore() != 0)
                m_attempt = Attempt::Restore;
        }
    });
    connect(facade, &BackendFacade::sessionChanged, this,
            [this](const domain::SessionSnapshot& session) {
                // A new token drops an SMS code still on its way (backend contract).
                if (m_state.smsState == account::SmsSending && session.token != m_smsToken) {
                    m_smsCommand = 0;
                    m_state.smsState = account::SmsIdle;
                }
                if (session.phase == domain::SessionPhase::Guest ||
                    session.phase == domain::SessionPhase::Expired) {
                    m_state.formBusy = false;
                    m_state.accountKey.clear();
                    m_state.nickname.clear();
                    if (m_attempt == Attempt::Qr && m_state.qrPhase != account::QrExpired &&
                        m_state.qrPhase != account::QrFailed)
                        m_state.qrPhase = account::NoQr;
                } else if (session.phase == domain::SessionPhase::Authenticated) {
                    m_state.accountKey = session.accountId;
                    m_state.formBusy = false;
                    m_state.qrPhase = account::NoQr;
                    m_state.qrImage = {};
                    m_attempt = Attempt::None;
                }
                applyBackend();
            });
    connect(facade, &BackendFacade::challengeReady, this,
            [this](domain::SessionToken, const domain::QrLoginChallenge& challenge) {
                if (m_attempt != Attempt::Qr)
                    return;
                m_state.qrImage = qrImage(challenge.loginUrl);
                ++m_state.qrSerial;
                m_state.qrPhase =
                    m_state.qrImage.isNull() ? account::QrFailed : account::WaitingForScan;
                notify();
            });
    connect(facade, &BackendFacade::statusChanged, this,
            [this](domain::SessionToken, domain::QrLoginStatus status) {
                if (m_attempt != Attempt::Qr)
                    return;
                m_state.qrPhase = phaseOf(status);
                if (status == domain::QrLoginStatus::Expired)
                    m_attempt = Attempt::None;
                notify();
            });
    connect(facade, &BackendFacade::signedIn, this,
            [this](domain::SessionToken, const domain::AccountIdentity& identity) {
                m_state.accountKey = identity.accountId;
                m_state.nickname = identity.nickname;
                m_state.formError.clear();
                applyBackend();
            });
    connect(facade, &BackendFacade::failed, this,
            [this](domain::SessionToken, const AuthFailure& failure) {
                switch (std::exchange(m_attempt, Attempt::None)) {
                case Attempt::Qr:
                    m_state.qrPhase = account::QrFailed;
                    break;
                case Attempt::Form:
                    m_state.formBusy = false;
                    m_state.formError = failureText(failure, false);
                    if (m_state.formError.isEmpty())
                        m_state.formError = tr("登录失败");
                    break;
                case Attempt::Restore:
                    qCWarning(lcUiBackend)
                        << log::event("account.restore_failed") << log::field("err", failure.error);
                    // No usable record (the backend forgets a damaged one): stop trying.
                    if (failure.error == AuthError::CredentialRejected)
                        QSettings().setValue(kRememberedKey, false);
                    break;
                case Attempt::None:
                    break;
                }
                applyBackend();
            });
    connect(facade, &BackendFacade::savedSessionCompleted, this,
            [](domain::SessionToken, const application::SavedSessionResult& result) {
                if (const auto* saved = std::get_if<bool>(&result))
                    QSettings().setValue(kRememberedKey, *saved);
                else
                    qCWarning(lcUiBackend) << log::event("account.save_failed");
            });
    connect(facade, &BackendFacade::smsCodeSent, this,
            [this](domain::SessionToken, const application::SmsCodeResult& result) {
                m_smsCommand = 0;
                if (std::holds_alternative<bool>(result)) {
                    m_state.smsState = account::SmsSent;
                    m_state.smsError.clear();
                    m_state.smsResendAt = QDateTime::currentMSecsSinceEpoch() + kResendMs;
                } else {
                    m_state.smsState = account::SmsIdle;
                    m_state.smsError = failureText(std::get<AuthFailure>(result), true);
                }
                notify();
            });
    connect(facade, &BackendFacade::acknowledged, this, [this](quint64 id, bool accepted) {
        if (id == m_smsCommand) {
            if (!accepted) {
                m_smsCommand = 0;
                m_state.smsState = account::SmsIdle;
                m_state.smsError = tr("暂时无法发送验证码");
                notify();
            } else {
                m_smsCommand = 0; // the result follows through smsCodeSent
            }
        } else if (id == m_formCommand) {
            m_formCommand = 0;
            if (!accepted && m_attempt == Attempt::Form) {
                m_attempt = Attempt::None;
                m_state.formBusy = false;
                m_state.formError = tr("暂时无法登录");
                notify();
            }
        }
    });
    applyBackend();
}

BackendSession::~BackendSession() = default;

void BackendSession::setChangedHandler(std::function<void()> changed)
{
    m_changed = std::move(changed);
}

void BackendSession::applyBackend()
{
    if (!m_facade) {
        m_state.status = account::Unavailable;
        notify();
        return;
    }
    switch (m_facade->state()) {
    case BackendState::Dormant:
    case BackendState::Starting:
        m_state.status = account::Starting;
        break;
    case BackendState::Failed:
        m_state.status = account::Unavailable;
        break;
    case BackendState::Stopping:
    case BackendState::Stopped:
        break; // quitting: keep what the window shows
    case BackendState::Ready:
        switch (m_facade->snapshot().phase) {
        case domain::SessionPhase::Guest:
            m_state.status = account::Guest;
            break;
        case domain::SessionPhase::Authenticating:
        case domain::SessionPhase::Committing:
            m_state.status = account::SigningIn;
            break;
        case domain::SessionPhase::Authenticated:
            m_state.status = account::SignedIn;
            break;
        case domain::SessionPhase::Expired:
            m_state.status = account::Expired;
            break;
        }
        break;
    }
    notify();
}

void BackendSession::beginQrLogin(bool remember)
{
    if (!m_facade || m_facade->state() != BackendState::Ready ||
        m_state.status == account::SignedIn)
        return;
    application::QrLoginOptions options;
    options.autoPoll = true;
    options.rememberSession = remember;
    m_remember = remember;
    m_attempt = Attempt::Qr;
    m_state.qrPhase = account::NoQr;
    m_state.qrImage = {};
    m_state.formBusy = false;
    if (m_facade->beginLogin(options) == 0) {
        m_attempt = Attempt::None;
        m_state.qrPhase = account::QrFailed;
    }
    notify();
}

void BackendSession::refreshQr() { beginQrLogin(m_remember); }

void BackendSession::cancelLogin()
{
    if (!m_facade || m_attempt == Attempt::None)
        return;
    m_attempt = Attempt::None;
    m_state.qrPhase = account::NoQr;
    m_state.qrImage = {};
    m_state.formBusy = false;
    static_cast<void>(m_facade->cancel());
    notify();
}

void BackendSession::signOut()
{
    if (m_facade)
        static_cast<void>(m_facade->signOut());
}

void BackendSession::beginFormLogin(account::FormMethod method, const QString& countryCode,
                                    const QString& user, const QString& secret, bool remember)
{
    if (!m_facade || m_facade->state() != BackendState::Ready || m_state.formBusy ||
        m_state.status == account::SignedIn)
        return;
    domain::CredentialLogin login;
    switch (method) {
    case account::PhoneCode:
        login = {domain::CredentialMethod::PhoneCode, countryCode, user.trimmed(),
                 secret.trimmed()};
        break;
    case account::PhonePassword:
        login = {domain::CredentialMethod::PhonePassword, countryCode, user.trimmed(),
                 domain::passwordDigest(secret)};
        break;
    case account::EmailPassword:
        login = {domain::CredentialMethod::EmailPassword,
                 {},
                 user.trimmed(),
                 domain::passwordDigest(secret)};
        break;
    }
    if (!domain::validCredentialLogin(login)) {
        if (method == account::EmailPassword && !domain::validEmail(login.account))
            m_state.formError = tr("邮箱格式不正确");
        else if (method != account::EmailPassword && !domain::validPhoneNumber(login.account))
            m_state.formError = tr("手机号格式不正确");
        else if (method == account::PhoneCode)
            m_state.formError = tr("验证码格式不正确");
        else
            m_state.formError = tr("请输入密码");
        notify();
        return;
    }
    application::QrLoginOptions options;
    options.rememberSession = remember;
    m_remember = remember;
    m_formCommand = m_facade->beginCredentialLogin(std::move(login), options);
    if (m_formCommand == 0) {
        m_state.formError = tr("暂时无法登录");
        notify();
        return;
    }
    m_attempt = Attempt::Form; // the QR attempt, if any, ends with this one
    m_state.formBusy = true;
    m_state.formError.clear();
    m_state.qrPhase = account::NoQr;
    m_state.qrImage = {};
    notify();
}

void BackendSession::sendSmsCode(const QString& countryCode, const QString& phone)
{
    if (!m_facade || m_state.smsState == account::SmsSending)
        return;
    const domain::SmsCodeRequest request{countryCode, phone.trimmed()};
    if (!domain::validSmsCodeRequest(request)) {
        m_state.smsError = tr("手机号格式不正确");
        notify();
        return;
    }
    m_smsToken = m_facade->snapshot().token;
    m_smsCommand = m_facade->sendSmsCode(request);
    if (m_smsCommand == 0) {
        m_state.smsError = tr("暂时无法发送验证码");
        notify();
        return;
    }
    m_state.smsState = account::SmsSending;
    m_state.smsError.clear();
    notify();
}

void BackendSession::clearFormError()
{
    if (m_state.formError.isEmpty() && m_state.smsError.isEmpty())
        return;
    m_state.formError.clear();
    m_state.smsError.clear();
    notify();
}

void BackendSession::notify()
{
    if (m_notifyPending)
        return;
    m_notifyPending = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_notifyPending = false;
            if (m_changed)
                m_changed();
        },
        Qt::QueuedConnection);
}

} // namespace cirrus::ui
