#include "network_status.h"

#include <QNetworkInformation>

namespace cirrus::ui
{

NetworkStatus::NetworkStatus(QObject* parent) : QObject(parent)
{
    if (!QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability))
        return;
    auto* info = QNetworkInformation::instance();
    const auto apply = [this](QNetworkInformation::Reachability reachability) {
        // Unknown counts as online: requests then report their own failures.
        setSystemOnline(reachability != QNetworkInformation::Reachability::Disconnected);
    };
    apply(info->reachability());
    connect(info, &QNetworkInformation::reachabilityChanged, this, apply);
}

void NetworkStatus::setSimulatedOffline(bool offline)
{
    if (m_simulatedOffline == offline)
        return;
    const bool wasOnline = online();
    m_simulatedOffline = offline;
    if (online() != wasOnline)
        emit onlineChanged();
}

void NetworkStatus::setSystemOnline(bool online)
{
    if (m_systemOnline == online)
        return;
    const bool wasOnline = this->online();
    m_systemOnline = online;
    if (this->online() != wasOnline)
        emit onlineChanged();
}

} // namespace cirrus::ui
