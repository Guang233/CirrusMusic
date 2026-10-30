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

Session::Session(SessionSource* source, QObject* parent)
    : QObject(parent), m_source(source), m_qrLogin(new QrLogin(this))
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

} // namespace cirrus::ui
