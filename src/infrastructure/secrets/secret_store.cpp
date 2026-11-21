#include "secret_store.h"
#include "logging/log.h"

#include <qt6keychain/keychain.h>

#include <QElapsedTimer>
#include <QPointer>

namespace cirrus::secrets
{
namespace
{

SecretError mapError(QKeychain::Error error)
{
    switch (error) {
    case QKeychain::NoError:
        return SecretError::None;
    case QKeychain::EntryNotFound:
        return SecretError::NotFound;
    case QKeychain::NoBackendAvailable:
    case QKeychain::NotImplemented:
        return SecretError::Unavailable;
    case QKeychain::AccessDeniedByUser:
    case QKeychain::AccessDenied:
        return SecretError::Denied;
    case QKeychain::CouldNotDeleteEntry:
    case QKeychain::OtherError:
        break;
    }
    return SecretError::Failed;
}

// Every keyring result (docs/tasks/logging.md §8): the entry's fixed name, never its data.
void logResult(const char* op, const QString& key, SecretError error, qint64 ms)
{
    if (error == SecretError::None || (error == SecretError::NotFound && op[0] == 'r'))
        log::info(lcSecrets, "keyring.done",
                  {{"op", op}, {"key", key}, {"err", error}, {"ms", ms}});
    else
        log::warn(lcSecrets, "keyring.failed",
                  {{"op", op}, {"key", key}, {"err", error}, {"ms", ms}});
}

template <typename Callback> void defer(QObject* context, Callback callback)
{
    QMetaObject::invokeMethod(context, std::move(callback), Qt::QueuedConnection);
}

// Common job setup; the context-bound connection drops results for dead contexts.
template <typename Job, typename OnFinished>
void startJob(Job* job, const char* op, const QString& key, QObject* context, OnFinished onFinished)
{
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(key);
    QElapsedTimer clock;
    clock.start();
    QObject::connect(
        job, &QKeychain::Job::finished, context,
        [context, op, key, clock, onFinished = std::move(onFinished)](QKeychain::Job* finished) {
            const auto error = mapError(finished->error());
            logResult(op, key, error, clock.elapsed());
            // Re-defer: some backends finish synchronously inside start().
            defer(context, [onFinished, error] { onFinished(error); });
        });
    job->start();
}

} // namespace

bool SecretStore::validKey(const QString& key)
{
    if (key.isEmpty() || key.size() > 64) {
        return false;
    }
    for (const auto c : key) {
        const auto u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '.' || u == '_' ||
              u == '-')) {
            return false;
        }
    }
    return true;
}

KeychainSecretStore::KeychainSecretStore(QString service) : m_service(std::move(service)) {}

void KeychainSecretStore::read(const QString& key, QObject* context,
                               std::function<void(SecretResult)> done)
{
    Q_ASSERT(context);
    if (!validKey(key) || m_service.isEmpty()) {
        defer(context, [done] { done({SecretError::Failed, {}}); });
        return;
    }
    auto* job = new QKeychain::ReadPasswordJob(m_service);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(key);
    // The job is deleted after finished(); copy the data out inside the signal.
    QElapsedTimer clock;
    clock.start();
    QObject::connect(job, &QKeychain::Job::finished, context,
                     [context, done, key, clock](QKeychain::Job* finished) {
                         SecretResult result{mapError(finished->error()), {}};
                         logResult("read", key, result.error, clock.elapsed());
                         if (result.ok()) {
                             result.data =
                                 static_cast<QKeychain::ReadPasswordJob*>(finished)->binaryData();
                         }
                         defer(context, [done, result = std::move(result)] { done(result); });
                     });
    job->start();
}

void KeychainSecretStore::write(const QString& key, const QByteArray& data, QObject* context,
                                std::function<void(SecretError)> done)
{
    Q_ASSERT(context);
    if (!validKey(key) || m_service.isEmpty()) {
        defer(context, [done] { done(SecretError::Failed); });
        return;
    }
    auto* job = new QKeychain::WritePasswordJob(m_service);
    job->setBinaryData(data);
    startJob(job, "write", key, context, std::move(done));
}

void KeychainSecretStore::remove(const QString& key, QObject* context,
                                 std::function<void(SecretError)> done)
{
    Q_ASSERT(context);
    if (!validKey(key) || m_service.isEmpty()) {
        defer(context, [done] { done(SecretError::Failed); });
        return;
    }
    startJob(new QKeychain::DeletePasswordJob(m_service), "remove", key, context, std::move(done));
}

} // namespace cirrus::secrets
