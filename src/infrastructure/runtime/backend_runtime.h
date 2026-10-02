#pragma once

#include "application/backend/backend_facade.h"

#include <functional>
#include <memory>

namespace cirrus::netease
{
class HttpTransport;
}
namespace cirrus::secrets
{
class SecretStore;
}

namespace cirrus::runtime
{

struct BackendConfig {
    QString databasePath;    // absolute installation state.sqlite path; not a credentials file
    QString keychainService; // caller-selected application identity; no invented app ID
};

// Factories run on the backend thread, never return pre-created GUI-thread QObjects.
// Empty factories select native Qt Network and QtKeychain. Offline tests inject both.
struct BackendFactories {
    std::function<std::unique_ptr<secrets::SecretStore>()> secrets;
    std::function<std::unique_ptr<netease::HttpTransport>()> apiTransport;
    std::function<std::unique_ptr<netease::HttpTransport>()> keyTransport;
};

// Composition root only; the template app does not instantiate it yet.
// Owns a caller-thread facade and a self-draining backend thread. Destruction
// requests asynchronous close, never blocks the caller. The process event loop
// must stay alive until facade.stopped(); destroying this handle is NOT erasure.
class BackendRuntime final : public QObject
{
    Q_OBJECT
  public:
    BackendRuntime(BackendConfig config, BackendFactories factories = {},
                   QObject* parent = nullptr);
    ~BackendRuntime() override;
    [[nodiscard]] application::BackendFacade& facade() const { return *m_facade; }

  private:
    application::BackendFacade* m_facade = nullptr;
};

} // namespace cirrus::runtime
