#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class NetworkStatus;

// Knobs of the fake data sources (App.debug), so every page state can be triggered
// in the gallery and in development builds (docs/ui/presentation.md §7).
class FakeControls : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.debug")
    // Reply delay; each request varies it by ±30 %. 0 replies on the next event loop turn.
    Q_PROPERTY(int delayMs READ delayMs WRITE setDelayMs NOTIFY delayMsChanged)
    // Failure.Kind for the next request only, -1 for none.
    Q_PROPERTY(int failNext READ failNext WRITE setFailNext NOTIFY failNextChanged)
    // Every request fails with Failure.Network and App.network reports offline.
    Q_PROPERTY(bool offline READ offline WRITE setOffline NOTIFY offlineChanged)
    // Lists come back empty.
    Q_PROPERTY(bool empty READ empty WRITE setEmpty NOTIFY emptyChanged)
    // Results are marked partial.
    Q_PROPERTY(bool partial READ partial WRITE setPartial NOTIFY partialChanged)

  public:
    explicit FakeControls(QObject* parent = nullptr);

    [[nodiscard]] int delayMs() const { return m_delayMs; }
    void setDelayMs(int delay);
    [[nodiscard]] int failNext() const { return m_failNext; }
    void setFailNext(int kind);
    [[nodiscard]] bool offline() const { return m_offline; }
    void setOffline(bool offline);
    [[nodiscard]] bool empty() const { return m_empty; }
    void setEmpty(bool empty);
    [[nodiscard]] bool partial() const { return m_partial; }
    void setPartial(bool partial);

    // Delay for one request, with jitter.
    [[nodiscard]] int nextDelay() const;
    // The failure to inject into the request being started, if any (consumes failNext).
    [[nodiscard]] int takeFailure();
    // Offline simulation also drives App.network.
    void attach(NetworkStatus* network);

  signals:
    void delayMsChanged();
    void failNextChanged();
    void offlineChanged();
    void emptyChanged();
    void partialChanged();

  private:
    int m_delayMs = 300;
    int m_failNext = -1;
    bool m_offline = false;
    bool m_empty = false;
    bool m_partial = false;
    NetworkStatus* m_network = nullptr;
};

} // namespace cirrus::ui
