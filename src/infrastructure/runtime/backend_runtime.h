#pragma once

#include "application/backend/backend_facade.h"

#include <functional>
#include <memory>

namespace cirrus::netease
{
class HttpTransport;
class CoverUpload;
class CommentV3Issuer;
} // namespace cirrus::netease
namespace cirrus::application
{
class IPlaybackBackend;
class ListeningSession;
class PersonalFmSource;
} // namespace cirrus::application
namespace cirrus::secrets
{
class SecretStore;
}

namespace cirrus::storage
{
class ListeningStateService;
}

namespace cirrus::cache
{
class ResourceCacheService;
class CachedMusicQueryService;
} // namespace cirrus::cache

namespace cirrus::runtime
{

struct BackendConfig {
    QString databasePath;    // absolute installation state.sqlite path; not a credentials file
    QString keychainService; // caller-selected application identity; no invented app ID
    bool persistListeningState = false; // explicit local metadata persistence; no product opt-in
    QString resourceCacheRoot{};        // empty=disabled; explicit existing private Linux directory
};

// Factories run on the backend thread, never return pre-created GUI-thread QObjects.
// Empty factories select native Qt Network and QtKeychain. Offline tests inject both.
struct BackendFactories {
    std::function<std::unique_ptr<secrets::SecretStore>()> secrets;
    std::function<std::unique_ptr<netease::HttpTransport>()> apiTransport;
    std::function<std::unique_ptr<netease::HttpTransport>()> keyTransport;
    std::function<std::unique_ptr<application::IPlaybackBackend>()> playback;
    std::function<std::unique_ptr<netease::CoverUpload>()> coverUpload; // same-thread NOS test seam
    std::function<std::unique_ptr<netease::CommentV3Issuer>()>
        commentV3Issuer; // same-thread, no real issuer in offline tests
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
    [[nodiscard]] application::ListeningSession& listening() const { return *m_listening; }
    [[nodiscard]] application::PersonalFmSource& fm() const { return *m_fm; }
    [[nodiscard]] storage::ListeningStateService* listeningState() const { return m_state; }
    [[nodiscard]] cache::ResourceCacheService* resourceCache() const { return m_cache; }
    [[nodiscard]] cache::CachedMusicQueryService* resourceQueries() const
    {
        return m_resourceQueries;
    }

  private:
    application::BackendFacade* m_facade = nullptr;
    application::PersonalFmSource* m_fm = nullptr;
    application::ListeningSession* m_listening = nullptr;
    storage::ListeningStateService* m_state = nullptr;
    cache::ResourceCacheService* m_cache = nullptr;
    cache::CachedMusicQueryService* m_resourceQueries = nullptr;
};

} // namespace cirrus::runtime
