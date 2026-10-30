#include "fake_controls.h"

#include "account.h"
#include "network_status.h"
#include "request.h"

#include <QRandomGenerator>

namespace cirrus::ui
{

FakeControls::FakeControls(QObject* parent) : QObject(parent), m_account(account::Guest) {}

void FakeControls::setAccount(int account)
{
    if (account != account::Guest && account != account::SignedIn && account != account::Expired)
        return;
    if (m_account == account)
        return;
    m_account = account;
    emit accountChanged();
}

bool FakeControls::signedIn() const { return m_account == account::SignedIn; }

void FakeControls::setDelayMs(int delay)
{
    delay = qMax(0, delay);
    if (m_delayMs == delay)
        return;
    m_delayMs = delay;
    emit delayMsChanged();
}

void FakeControls::setFailNext(int kind)
{
    if (m_failNext == kind)
        return;
    m_failNext = kind;
    emit failNextChanged();
}

void FakeControls::setOffline(bool offline)
{
    if (m_offline == offline)
        return;
    m_offline = offline;
    if (m_network)
        m_network->setSimulatedOffline(offline);
    emit offlineChanged();
}

void FakeControls::setEmpty(bool empty)
{
    if (m_empty == empty)
        return;
    m_empty = empty;
    emit emptyChanged();
}

void FakeControls::setPartial(bool partial)
{
    if (m_partial == partial)
        return;
    m_partial = partial;
    emit partialChanged();
}

int FakeControls::nextDelay() const
{
    if (m_delayMs == 0)
        return 0;
    const double jitter = 0.7 + 0.6 * QRandomGenerator::global()->generateDouble();
    return int(m_delayMs * jitter);
}

int FakeControls::takeFailure()
{
    if (m_offline)
        return failure::Network;
    const int kind = m_failNext;
    setFailNext(-1);
    return kind;
}

void FakeControls::attach(NetworkStatus* network)
{
    m_network = network;
    if (m_network)
        m_network->setSimulatedOffline(m_offline);
}

} // namespace cirrus::ui
