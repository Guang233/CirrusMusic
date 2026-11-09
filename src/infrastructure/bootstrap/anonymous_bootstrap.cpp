#include "anonymous_bootstrap.h"
#include "logging/log.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>

namespace cirrus::bootstrap
{
namespace
{

QByteArray serializeToken(const QString& deviceId, const QByteArray& token)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                     {QStringLiteral("deviceId"), deviceId},
                                     {QStringLiteral("token"), QString::fromLatin1(token)}})
        .toJson(QJsonDocument::Compact);
}

// Empty unless the record is well-formed AND belongs to this device ID.
QByteArray tokenFor(const QString& deviceId, const QByteArray& data)
{
    if (data.size() > 8192) {
        return {};
    }
    const auto document = QJsonDocument::fromJson(data);
    const auto object = document.object();
    if (!document.isObject() || object.value(QStringLiteral("format")) != QJsonValue(1) ||
        object.value(QStringLiteral("deviceId")).toString() != deviceId) {
        return {};
    }
    const auto text = object.value(QStringLiteral("token")).toString();
    const auto token = text.toLatin1();
    return QString::fromLatin1(token) == text && netease::validAnonymousToken(token) ? token
                                                                                     : QByteArray();
}

} // namespace

AnonymousBootstrap::AnonymousBootstrap(netease::SessionApiClient& client,
                                       netease::XeapiKeyRing& ring,
                                       netease::XeapiKeyRegistrar& registrar, XeapiKeyVault& vault,
                                       secrets::SecretStore& store, QString deviceId,
                                       ContextProvider context, QObject* parent)
    : QObject(parent), m_client(client), m_ring(ring), m_registrar(registrar), m_vault(vault),
      m_store(store), m_deviceId(std::move(deviceId)), m_context(std::move(context))
{
    Q_ASSERT(client.thread() == thread() && ring.thread() == thread() &&
             registrar.thread() == thread() && vault.thread() == thread());
    connect(&vault, &XeapiKeyVault::restored, this, &AnonymousBootstrap::keyRestored);
    connect(&registrar, &netease::XeapiKeyRegistrar::finished, this,
            &AnonymousBootstrap::keyRegistered);
    connect(&client, &netease::SessionApiClient::completed, this, &AnonymousBootstrap::registered);
}

AnonymousBootstrap::~AnonymousBootstrap()
{
    // Shared key restoration/registration belongs to the installation, not this run.
    disconnect(&m_client, nullptr, this, nullptr);
    if (m_request != 0) {
        m_client.cancel(m_request);
    }
}

bool AnonymousBootstrap::currentContext()
{
    if (m_client.accepts(m_attempt)) {
        return true;
    }
    BootstrapResult result{BootstrapError::RegisterFailed};
    result.apiError = netease::ApiError::StaleContext;
    complete(result);
    return false;
}

bool AnonymousBootstrap::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stage != Stage::Idle || m_resetting) {
        return false;
    }
    m_stage = Stage::Key;
    m_attempt = m_context ? m_context() : domain::SessionToken{};
    if (m_ring.current()) {
        QTimer::singleShot(0, this, [this] {
            if (m_stage != Stage::Key) {
                return;
            }
            if (m_resetting) {
                eraseToken();
            } else {
                restoreToken();
            }
        }); // deferred even when the key is already present
    } else {
        m_vault.restore();
    }
    return true;
}

void AnonymousBootstrap::keyRestored()
{
    if (m_stage != Stage::Key) {
        return; // a restore requested by someone else
    }
    if (m_resetting) {
        eraseToken();
        return;
    }
    if (m_ring.current()) {
        restoreToken();
        return;
    }
    m_stage = Stage::Registering;
    // false = a refresh is already in flight; its finished() continues this sequence.
    static_cast<void>(m_registrar.refresh());
}

void AnonymousBootstrap::keyRegistered(const netease::XeapiKeyResult& result)
{
    if (m_stage != Stage::Registering) {
        return;
    }
    if (m_resetting) {
        eraseToken();
        return;
    }
    if (!m_ring.current()) {
        BootstrapResult failed{BootstrapError::KeyUnavailable};
        failed.keyError = result.error;
        complete(failed);
        return;
    }
    restoreToken(); // the vault persists the new key on its own
}

void AnonymousBootstrap::restoreToken()
{
    if (!currentContext()) {
        return;
    }
    if (m_ignoreStoredToken) {
        registerAnonymous();
        return;
    }
    m_stage = Stage::Token;
    m_store.read(QString::fromLatin1(TokenEntryKey), this, [this](secrets::SecretResult stored) {
        if (m_resetting) {
            stored.data.fill('\0');
            eraseToken();
            return;
        }
        if (m_stage != Stage::Token || !currentContext()) {
            stored.data.fill('\0');
            return;
        }
        auto token = stored.ok() ? tokenFor(m_deviceId, stored.data) : QByteArray();
        stored.data.fill('\0');
        if (token.isEmpty()) {
            registerAnonymous(); // absent, unreadable, damaged or another device's token
            return;
        }
        m_client.setAnonymousToken(token);
        token.fill('\0');
        BootstrapResult result;
        result.restored = true;
        complete(result);
    });
}

void AnonymousBootstrap::registerAnonymous()
{
    m_stage = Stage::Anonymous;
    if (!currentContext()) {
        return;
    }
    m_request = m_client.submit(m_attempt, netease::ApiOperation::AnonymousRegister);
}

void AnonymousBootstrap::registered(quint64 id, domain::SessionToken context,
                                    const netease::ApiResult& api)
{
    if (m_stage != Stage::Anonymous || id != m_request) {
        return;
    }
    m_request = 0;
    if (m_resetting) {
        eraseToken();
        return;
    }
    if (context != m_attempt || !currentContext()) {
        return;
    }
    BootstrapResult result;
    result.apiError = api.error;
    result.businessCode = api.decoded.businessCode;
    if (api.error != netease::ApiError::None) {
        result.error = BootstrapError::RegisterFailed;
        complete(result);
        return;
    }
    if (api.decoded.businessCode != 200 || api.anonymousToken.isEmpty()) {
        result.error = BootstrapError::Rejected;
        complete(result);
        return;
    }
    m_client.setAnonymousToken(api.anonymousToken);
    m_stage = Stage::Persisting;
    auto record = serializeToken(m_deviceId, api.anonymousToken);
    m_store.write(QString::fromLatin1(TokenEntryKey), record, this,
                  [this, result](secrets::SecretError error) {
                      if (m_resetting) {
                          eraseToken(); // delete AFTER the write has settled
                          return;
                      }
                      if (error != secrets::SecretError::None) {
                          qCWarning(lcSecrets)
                              << log::event("guest.persist_failed") << log::field("err", error);
                          emit persistFailed(error); // still usable for this process
                      } else {
                          m_ignoreStoredToken = false;
                      }
                      // persistFailed observers may synchronously request a reset.
                      if (m_resetting) {
                          eraseToken();
                      } else {
                          complete(result);
                      }
                  });
    record.fill('\0');
}

void AnonymousBootstrap::reset()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_client.setAnonymousToken({});
    m_ignoreStoredToken = true;
    if (m_resetting) {
        return; // coalesce resets; never overlap writes and deletion
    }
    m_resetting = true;
    if (m_request != 0) {
        m_client.cancel(m_request); // its deferred completion drains the run
    } else if (m_stage == Stage::Idle) {
        eraseToken();
    }
    // Otherwise wait for the current callback before deleting. start() stays Busy.
}

void AnonymousBootstrap::eraseToken()
{
    m_stage = Stage::Erasing;
    m_store.remove(QString::fromLatin1(TokenEntryKey), this, [this](secrets::SecretError error) {
        if (error != secrets::SecretError::None && error != secrets::SecretError::NotFound) {
            emit persistFailed(error);
        }
        m_resetting = false;
        complete({BootstrapError::Canceled});
    });
}

void AnonymousBootstrap::complete(BootstrapResult result)
{
    if (result.error == BootstrapError::None || result.error == BootstrapError::Canceled)
        qCInfo(lcAuth) << log::event("guest.bootstrap") << log::field("err", result.error)
                       << log::field("restored", result.restored);
    else
        qCWarning(lcAuth) << log::event("guest.bootstrap") << log::field("err", result.error)
                          << log::field("key_err", result.keyError)
                          << log::field("api_err", result.apiError)
                          << log::field("code", result.businessCode.value_or(0));
    m_stage = Stage::Idle;
    m_attempt = {};
    emit finished(result);
}

} // namespace cirrus::bootstrap
