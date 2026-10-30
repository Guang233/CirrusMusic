#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Whether the system has a network (App.network), from QNetworkInformation. Assumed
// online when the platform has no reachability backend. Fakes can simulate offline.
class NetworkStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.network")
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)

  public:
    explicit NetworkStatus(QObject* parent = nullptr);

    [[nodiscard]] bool online() const { return m_systemOnline && !m_simulatedOffline; }
    void setSimulatedOffline(bool offline);

  signals:
    void onlineChanged();

  private:
    void setSystemOnline(bool online);

    bool m_systemOnline = true;
    bool m_simulatedOffline = false;
};

} // namespace cirrus::ui
