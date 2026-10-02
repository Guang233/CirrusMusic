#pragma once

#include "infrastructure/netease/api/xeapi_key_ring.h"
#include "infrastructure/secrets/secret_store.h"

#include <QPointer>

namespace cirrus::bootstrap
{

// Persists the whole XEAPI key record (version, publicKey, sk) as ONE keyring
// entry, so the record can never be half-written across two stores. Ring and
// store must outlive the vault; all in one thread.
class XeapiKeyVault final : public QObject
{
    Q_OBJECT
  public:
    static constexpr char EntryKey[] = "xeapi-key";

    XeapiKeyVault(netease::XeapiKeyRing& ring, secrets::SecretStore& store,
                  QObject* parent = nullptr);

    // Loads a stored record into an EMPTY ring (never overwrites a newer key and
    // never writes back). Result via restored().
    void restore();
    // Read jobs are accounted for by the composition's TrackedSecretStore.
    [[nodiscard]] bool isIdle() const { return !m_busy && !m_dirty; }

    [[nodiscard]] static QByteArray serialize(const netease::XeapiPublicKey& key);
    [[nodiscard]] static std::optional<netease::XeapiPublicKey> deserialize(const QByteArray& data);

  signals:
    void restored(cirrus::secrets::SecretError error);
    void persistFailed(cirrus::secrets::SecretError error); // key still usable in memory

  private:
    void ringChanged();
    void flush();

    QPointer<netease::XeapiKeyRing> m_ring;
    secrets::SecretStore& m_store;
    bool m_restoring = false;
    bool m_busy = false;  // one storage job at a time
    bool m_dirty = false; // ring changed since the last job started
};

} // namespace cirrus::bootstrap
