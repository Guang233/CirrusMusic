#include "account_credential_vault.h"

#include <QThread>
#include <QTimer>
#include <utility>

namespace cirrus::netease
{

AccountCredentialVault::AccountCredentialVault(SessionApiClient& client,
                                               secrets::SecretStore& store, QObject* parent)
    : QObject(parent), m_client(&client), m_store(store)
{
    Q_ASSERT(client.thread() == thread());
}

void AccountCredentialVault::read(domain::SessionToken context, QObject* receiver,
                                  std::function<void(SavedCredentialResult)> done)
{
    Q_ASSERT(QThread::currentThread() == thread() && receiver && receiver->thread() == thread());
    if (m_read || m_forgotten || !m_client || !m_client->accepts(context)) {
        const bool stale = !m_client || !m_client->accepts(context);
        QTimer::singleShot(0, receiver,
                           [done, stale] { done({secrets::SecretError::NotFound, stale, {}}); });
        return;
    }
    m_read = Read{context, receiver, std::move(done)};
    QTimer::singleShot(0, this, [this] { flush(); });
}

void AccountCredentialVault::save(domain::SessionToken context, const QString& accountId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto record = m_client ? m_client->saveCredentials(context, accountId) : std::nullopt;
    if (!record) {
        QTimer::singleShot(0, this, [this, context] {
            emit settled(context, false, secrets::SecretError::Failed);
        });
        return;
    }
    ++m_revision;
    m_writeContext = context;
    m_forgotten = true; // never restore an older account after this save fails
    m_desired.fill('\0');
    m_desired = *record;
    m_dirty = true;
    QTimer::singleShot(0, this, [this] { flush(); });
}

void AccountCredentialVault::forget(domain::SessionToken context)
{
    Q_ASSERT(QThread::currentThread() == thread());
    ++m_revision;
    m_writeContext = context;
    m_forgotten = true;
    m_desired.fill('\0');
    m_desired.clear();
    m_dirty = true;
    QTimer::singleShot(0, this, [this] { flush(); });
}

void AccountCredentialVault::flush()
{
    if (m_busy) {
        return;
    }
    if (m_dirty) {
        m_busy = true;
        m_dirty = false;
        const auto revision = m_revision;
        const auto context = m_writeContext;
        const bool remembered = !m_desired.isEmpty();
        const auto done = [this, revision, context, remembered](secrets::SecretError error) {
            m_busy = false;
            if (!remembered && error == secrets::SecretError::NotFound) {
                error = secrets::SecretError::None;
            }
            if (revision == m_revision) {
                if (remembered && error == secrets::SecretError::None) {
                    m_forgotten = false;
                }
                emit settled(context, remembered, error);
            }
            flush(); // a newer save/logout supersedes the completed write
        };
        if (remembered) {
            m_store.write(QString::fromLatin1(EntryKey), m_desired, this, done);
        } else {
            m_store.remove(QString::fromLatin1(EntryKey), this, done);
        }
        return;
    }
    if (!m_read) {
        return;
    }
    const auto request = std::exchange(m_read, std::nullopt).value();
    if (!request.receiver) {
        return;
    }
    m_busy = true;
    const auto revision = m_revision;
    m_store.read(QString::fromLatin1(EntryKey), this,
                 [this, revision, request](secrets::SecretResult result) {
                     m_busy = false;
                     const bool stale = revision != m_revision || m_forgotten || !m_client ||
                                        !m_client->accepts(request.context);
                     if (request.receiver) {
                         request.done({result.error, stale, stale ? QByteArray{} : result.data});
                     }
                     result.data.fill('\0');
                     flush();
                 });
}

} // namespace cirrus::netease
