#pragma once
#include "native_media_inbox.h"
#include "native_media_service.h"

namespace cirrus::infrastructure::system_media
{
// Infrastructure-only platform port: only current projection, original scalar
// intents and clearing/revocation. Not a generic RPC, transport or media engine.
enum class NativeProjectionResult { Published, Retired, Failed };
class NativeMediaPlatform
{
  public:
    using Permit = std::function<bool()>;
    using Lost = std::function<void()>; // only on the platform/owner main thread
    virtual ~NativeMediaPlatform() = default;
    virtual bool start(const NativeMediaConfig&, NativeMediaInbox::Callback, Lost) = 0;
    virtual bool current() const = 0;
    // Recheck permit before each native field admission; it may reenter source
    // retirement. Already admitted OS/consumer values cannot be withdrawn.
    virtual NativeProjectionResult publish(const domain::PlaybackObservation&,
                                           const NativeMediaConfig&, const Permit&) = 0;
    virtual bool clear() noexcept = 0;
    virtual bool close() noexcept = 0;
};
std::shared_ptr<NativeMediaPlatform> createWindowsSmtcPlatform();
std::shared_ptr<NativeMediaPlatform> createMacNowPlayingPlatform();
} // namespace cirrus::infrastructure::system_media
