#pragma once

#include "infrastructure/secrets/secret_store.h"

#include <QHash>
#include <QPointer>

namespace cirrus::test
{

// In-memory keyring. Completions are deferred like the real store; when `hold`
// is set they wait until release() so tests can observe overlapping requests.
class FakeSecretStore final : public secrets::SecretStore
{
  public:
    QHash<QString, QByteArray> entries;
    secrets::SecretError failWith = secrets::SecretError::None;
    bool hold = false;
    int inFlight = 0;
    int maxInFlight = 0;
    int writes = 0;
    int removes = 0;

    void read(const QString& key, QObject* context,
              std::function<void(secrets::SecretResult)> done) override
    {
        const auto error = failWith != secrets::SecretError::None ? failWith
                           : entries.contains(key)                ? secrets::SecretError::None
                                                                  : secrets::SecretError::NotFound;
        schedule(context, [done, error, data = entries.value(key)] { done({error, data}); });
    }
    void write(const QString& key, const QByteArray& data, QObject* context,
               std::function<void(secrets::SecretError)> done) override
    {
        ++writes;
        const auto error = failWith;
        schedule(context, [this, done, error, key, data] {
            if (error == secrets::SecretError::None) {
                entries.insert(key, data);
            }
            done(error);
        });
    }
    void remove(const QString& key, QObject* context,
                std::function<void(secrets::SecretError)> done) override
    {
        ++removes;
        const auto error = failWith;
        schedule(context, [this, done, error, key] {
            const bool existed = entries.remove(key) > 0;
            done(error != secrets::SecretError::None ? error
                 : existed                           ? secrets::SecretError::None
                                                     : secrets::SecretError::NotFound);
        });
    }

    void release()
    {
        auto pending = std::move(m_held);
        m_held.clear();
        for (auto& run : pending) {
            run();
        }
    }

  private:
    void schedule(QObject* context, std::function<void()> complete)
    {
        maxInFlight = std::max(maxInFlight, ++inFlight);
        QPointer<QObject> target(context);
        auto run = [this, target, complete = std::move(complete)] {
            --inFlight;
            if (target) {
                complete();
            }
        };
        if (hold) {
            m_held.append(std::move(run));
        } else {
            QMetaObject::invokeMethod(context, std::move(run), Qt::QueuedConnection);
        }
    }
    QList<std::function<void()>> m_held;
};

} // namespace cirrus::test
