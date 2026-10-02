#pragma once

#include "secret_store.h"

#include <memory>

namespace cirrus::secrets
{

// Tracks backend completion even when the original recipient has died. The
// owner keeps this adapter AND its backend alive until pending() reaches zero.
// Completion counts include callbacks and any follow-up jobs they enqueue.
class TrackedSecretStore final : public QObject, public SecretStore
{
    Q_OBJECT
  public:
    explicit TrackedSecretStore(std::unique_ptr<SecretStore> backend);
    void read(const QString& key, QObject* context,
              std::function<void(SecretResult)> done) override;
    void write(const QString& key, const QByteArray& data, QObject* context,
               std::function<void(SecretError)> done) override;
    void remove(const QString& key, QObject* context,
                std::function<void(SecretError)> done) override;
    [[nodiscard]] int pending() const { return m_pending; }
    [[nodiscard]] bool writeFailed() const { return m_writeFailed; }

  signals:
    void idle(); // observation only; consumers recheck all producer queues before shutdown

  private:
    void begin(QObject* context);
    void end();
    std::unique_ptr<SecretStore> m_backend;
    int m_pending = 0;
    bool m_writeFailed = false;
};

} // namespace cirrus::secrets
