#include "tracked_secret_store.h"

#include <QPointer>
#include <QThread>

namespace cirrus::secrets
{

TrackedSecretStore::TrackedSecretStore(std::unique_ptr<SecretStore> backend)
    : m_backend(std::move(backend))
{
    Q_ASSERT(m_backend);
}

void TrackedSecretStore::begin(QObject* context)
{
    Q_ASSERT(QThread::currentThread() == thread() && context && context->thread() == thread());
    ++m_pending;
}

void TrackedSecretStore::end()
{
    if (--m_pending == 0) {
        emit idle();
    }
}

void TrackedSecretStore::read(const QString& key, QObject* context,
                              std::function<void(SecretResult)> done)
{
    begin(context);
    m_backend->read(key, this,
                    [this, receiver = QPointer<QObject>(context), done](SecretResult result) {
                        if (receiver) {
                            done(std::move(result));
                        }
                        end();
                    });
}

void TrackedSecretStore::write(const QString& key, const QByteArray& data, QObject* context,
                               std::function<void(SecretError)> done)
{
    begin(context);
    m_backend->write(key, data, this,
                     [this, receiver = QPointer<QObject>(context), done](SecretError error) {
                         m_writeFailed = m_writeFailed || error != SecretError::None;
                         if (receiver) {
                             done(error);
                         }
                         end();
                     });
}

void TrackedSecretStore::remove(const QString& key, QObject* context,
                                std::function<void(SecretError)> done)
{
    begin(context);
    m_backend->remove(
        key, this, [this, receiver = QPointer<QObject>(context), done](SecretError error) {
            m_writeFailed =
                m_writeFailed || (error != SecretError::None && error != SecretError::NotFound);
            if (receiver) {
                done(error);
            }
            end();
        });
}

} // namespace cirrus::secrets
