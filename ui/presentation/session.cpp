#include "session.h"

#include "sources.h"

namespace cirrus::ui
{

QString QrLogin::qrImage() const
{
    return m_phase == account::NoQr ? QString() : QStringLiteral("image://qr/%1").arg(m_serial);
}

void QrLogin::setRemember(bool remember)
{
    if (m_remember == remember)
        return;
    m_remember = remember;
    emit rememberChanged();
}

void QrLogin::update(account::QrPhase phase, int serial)
{
    if (m_phase == phase && m_serial == serial)
        return;
    m_phase = phase;
    m_serial = serial;
    emit changed();
}

void FormLogin::update(const SessionSnapshot& snapshot)
{
    if (m_busy == snapshot.formBusy && m_error == snapshot.formError &&
        m_smsState == snapshot.smsState && m_smsError == snapshot.smsError &&
        m_resendAt == snapshot.smsResendAt)
        return;
    m_busy = snapshot.formBusy;
    m_error = snapshot.formError;
    m_smsState = snapshot.smsState;
    m_smsError = snapshot.smsError;
    m_resendAt = snapshot.smsResendAt;
    emit changed();
}

Session::Session(SessionSource* source, QObject* parent)
    : QObject(parent), m_source(source), m_qrLogin(new QrLogin(this)),
      m_formLogin(new FormLogin(this))
{
    if (!m_source)
        return;
    m_source->setChangedHandler([this]() { update(); });
    update();
}

void Session::update()
{
    const SessionSnapshot& snapshot = m_source->snapshot();
    const bool accountChange = snapshot.accountKey != m_accountKey;
    m_status = snapshot.status;
    m_accountKey = snapshot.accountKey;
    m_nickname = snapshot.nickname;
    m_avatarKey = snapshot.avatarKey;
    m_qrLogin->update(snapshot.qrPhase, snapshot.qrSerial);
    m_formLogin->update(snapshot);
    emit changed();
    if (accountChange)
        emit accountChanged();
}

void Session::beginQrLogin()
{
    if (m_source)
        m_source->beginQrLogin(m_qrLogin->remember());
}

void Session::refreshQr()
{
    if (m_source)
        m_source->refreshQr();
}

void Session::cancelLogin()
{
    if (m_source)
        m_source->cancelLogin();
}

void Session::signOut()
{
    if (m_source)
        m_source->signOut();
}

void Session::beginFormLogin(int method, const QString& countryCode, const QString& user,
                             const QString& secret)
{
    if (m_source && method >= account::PhoneCode && method <= account::EmailPassword)
        m_source->beginFormLogin(account::FormMethod(method), countryCode, user, secret,
                                 m_qrLogin->remember());
}

void Session::sendSmsCode(const QString& countryCode, const QString& phone)
{
    if (m_source)
        m_source->sendSmsCode(countryCode, phone);
}

void Session::clearFormError()
{
    if (m_source)
        m_source->clearFormError();
}

} // namespace cirrus::ui
