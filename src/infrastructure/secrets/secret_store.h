#pragma once
#include "logging/log.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

namespace cirrus::secrets
{

enum class SecretError {
    None,
    NotFound,
    Unavailable, // no secure backend; never falls back to plaintext
    Denied,      // user or system refused access
    Failed
};
CIRRUS_LOG_ENUM(SecretError, None, NotFound, Unavailable, Denied, Failed)

struct SecretResult {
    SecretError error = SecretError::None;
    QByteArray data; // secret; callers should not log or persist it elsewhere
    [[nodiscard]] bool ok() const { return error == SecretError::None; }
};

// Asynchronous, thread-affine secret storage. Callbacks are always deferred and
// delivered only while context (same thread) is alive. Key names must match
// [a-z0-9._-]{1,64}; invalid names fail with Failed without touching the backend.
class SecretStore
{
  public:
    virtual ~SecretStore() = default;
    virtual void read(const QString& key, QObject* context,
                      std::function<void(SecretResult)> done) = 0;
    virtual void write(const QString& key, const QByteArray& data, QObject* context,
                       std::function<void(SecretError)> done) = 0;
    virtual void remove(const QString& key, QObject* context,
                        std::function<void(SecretError)> done) = 0;

    [[nodiscard]] static bool validKey(const QString& key);
};

// System keyring via QtKeychain (Secret Service / KWallet on Linux). Insecure
// plaintext fallback is explicitly disabled.
class KeychainSecretStore final : public SecretStore
{
  public:
    explicit KeychainSecretStore(QString service);
    void read(const QString& key, QObject* context,
              std::function<void(SecretResult)> done) override;
    void write(const QString& key, const QByteArray& data, QObject* context,
               std::function<void(SecretError)> done) override;
    void remove(const QString& key, QObject* context,
                std::function<void(SecretError)> done) override;

  private:
    const QString m_service;
};

} // namespace cirrus::secrets

Q_DECLARE_METATYPE(cirrus::secrets::SecretError)
