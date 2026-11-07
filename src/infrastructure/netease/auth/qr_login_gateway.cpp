#include "qr_login_gateway.h"
#include "account_credential_vault.h"
#include "account_identity_mapper.h"

#include <QThread>
#include <QTimer>

#include <utility>

namespace cirrus::netease
{
namespace
{
using application::AuthError;
using application::AuthFailure;

AuthFailure failureFor(const ApiResult& result)
{
    AuthFailure failure{AuthError::ProtocolFailure};
    switch (result.error) {
    case ApiError::None:
        failure.error = result.decoded.businessCode == 429 ? AuthError::RateLimited
                                                           : AuthError::ProviderRejected;
        failure.providerCode = result.decoded.businessCode;
        break;
    case ApiError::InvalidRequest:
        failure.error = AuthError::InvalidRequest;
        break;
    case ApiError::StaleContext:
        failure.error = AuthError::StaleContext;
        break;
    case ApiError::Canceled:
        failure.error = AuthError::Canceled;
        break;
    case ApiError::Busy:
        failure.error = AuthError::Busy;
        break;
    case ApiError::Crypto:
        break;
    case ApiError::Decode:
        failure.error = AuthError::InvalidResponse;
        break;
    case ApiError::Cookie:
        failure.error = AuthError::CredentialRejected;
        break;
    case ApiError::Transport:
        switch (result.transportError) {
        case TransportError::Canceled:
            failure.error = AuthError::Canceled;
            break;
        case TransportError::Timeout:
            failure.error = AuthError::Timeout;
            break;
        case TransportError::InvalidRequest:
            failure.error = AuthError::InvalidRequest;
            break;
        case TransportError::Network:
            failure.error = AuthError::Network;
            break;
        case TransportError::HttpStatus:
            failure.error =
                result.httpStatus == 429 ? AuthError::RateLimited : AuthError::ProviderRejected;
            break;
        case TransportError::ResponseTooLarge:
        case TransportError::ResponseHeadersTooLarge:
        case TransportError::RedirectBlocked:
        case TransportError::None:
            failure.error = AuthError::InvalidResponse;
            break;
        }
        break;
    }
    if (failure.error == AuthError::RateLimited && !result.retryAfter.isEmpty()) {
        bool digits = result.retryAfter.size() <= 5;
        for (const auto c : result.retryAfter) {
            digits = digits && c >= '0' && c <= '9';
        }
        bool ok = false;
        const auto seconds = result.retryAfter.toLongLong(&ok);
        if (digits && ok && seconds <= 86400) {
            failure.retryAfterMs = seconds * 1000;
        }
        // HTTP-date/unknown forms remain unknown, not a zero-delay retry.
    }
    return failure;
}

} // namespace

QrLoginGateway::QrLoginGateway(SessionApiClient& client, QObject* parent)
    : IQrLoginGateway(parent), m_client(&client)
{
    Q_ASSERT(client.thread() == thread());
    connect(&client, &SessionApiClient::completed, this, &QrLoginGateway::receive);
    connect(&client, &SessionApiClient::contextAdvanced, this,
            [this](const domain::SessionToken& context) {
                if (m_client && m_client->accepts(context)) {
                    m_key.clear();
                    m_keyContext = {};
                    m_authorizedContext = {};
                    m_verified.reset();
                    m_boundContext = {};
                    m_boundAccount.clear();
                    m_restoreId = 0;
                    // Release the single-flight slot NOW, including pre-dispatch
                    // calls. Old completion still carries its original token.
                    const auto ids = m_pending.keys();
                    for (const auto id : ids) {
                        auto it = m_pending.find(id);
                        if (it != m_pending.end() && it->context != context) {
                            const auto apiId = it->apiId;
                            m_apiIds.remove(apiId);
                            it->apiId = 0;
                            if (m_promotionId == id) {
                                m_promotionId = 0;
                            }
                            if (m_smsId == id) {
                                m_smsApiId = 0; // m_smsId clears when the rejection lands
                            }
                            rejectLater(id, {AuthError::StaleContext});
                            if (apiId != 0 && m_client) {
                                m_client->cancel(apiId);
                            }
                        }
                    }
                }
            });
    connect(&client, &QObject::destroyed, this, [this] {
        m_key.clear();
        m_keyContext = {};
        m_authorizedContext = {};
        m_verified.reset();
        m_promotionId = 0;
        m_smsApiId = 0;
        m_apiIds.clear();
        const auto ids = m_pending.keys();
        for (const auto id : ids) {
            rejectLater(id, {AuthError::Network});
        }
    });
}

QrLoginGateway::QrLoginGateway(SessionApiClient& client, AccountCredentialVault& vault,
                               QObject* parent)
    : QrLoginGateway(client, parent)
{
    Q_ASSERT(vault.thread() == thread());
    m_vault = &vault;
    connect(&vault, &AccountCredentialVault::settled, this,
            [this](domain::SessionToken context, bool remembered, secrets::SecretError error) {
                if (error == secrets::SecretError::None) {
                    emit savedSessionCompleted(context, remembered);
                } else {
                    emit savedSessionCompleted(context, AuthFailure{AuthError::CredentialRejected});
                }
            });
    connect(&vault, &QObject::destroyed, this, [this] {
        if (m_restoreId != 0) {
            rejectLater(m_restoreId, {AuthError::CredentialRejected});
        }
    });
}

quint64 QrLoginGateway::restoreIdentity(domain::SessionToken context)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, ApiOperation::AccountIdentity});
    if (!m_client || !m_client->accepts(context)) {
        rejectLater(id, {AuthError::StaleContext});
    } else if (!m_vault) {
        rejectLater(id, {AuthError::CredentialRejected});
    } else if (!m_apiIds.isEmpty() || m_promotionId != 0 || m_restoreId != 0) {
        rejectLater(id, {AuthError::Busy});
    } else {
        m_restoreId = id;
        m_vault->read(context, this, [this, id, context](SavedCredentialResult result) {
            const auto it = m_pending.constFind(id);
            if (it == m_pending.constEnd() || it->failure) {
                result.record.fill('\0');
                return;
            }
            if (result.stale || !m_client || !m_client->accepts(context)) {
                result.record.fill('\0');
                finishFailure(id, {AuthError::StaleContext});
                return;
            }
            const auto account = result.error == secrets::SecretError::None
                                     ? m_client->restoreCredentials(context, result.record)
                                     : std::nullopt;
            result.record.fill('\0');
            if (!account) {
                if (result.error == secrets::SecretError::None && m_vault) {
                    m_vault->forget(context); // damaged/expired record, not a network failure
                }
                finishFailure(id, {AuthError::CredentialRejected});
                return;
            }
            m_authorizedContext = context; // only enables identity verification, not login
            m_pending[id].restoredAccount = *account;
            const auto apiId = m_client->submit(context, ApiOperation::AccountIdentity);
            m_pending[id].apiId = apiId;
            m_apiIds.insert(apiId, id);
        });
    }
    return id;
}

void QrLoginGateway::storeSession(domain::SessionToken context, domain::AccountIdentity identity,
                                  bool remember)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_client || !m_client->accepts(context) || m_boundContext != context ||
        m_boundAccount != identity.accountId) {
        return;
    }
    if (m_vault) {
        if (remember) {
            m_vault->save(context, identity.accountId);
        } else {
            m_vault->forget(context);
        }
    } else if (remember) {
        QTimer::singleShot(0, this, [this, context] {
            emit savedSessionCompleted(context, AuthFailure{AuthError::CredentialRejected});
        });
    }
}

void QrLoginGateway::forgetSession(domain::SessionToken context)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_vault && m_client && m_client->accepts(context)) {
        m_vault->forget(context);
    }
}

QrLoginGateway::~QrLoginGateway()
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        if (m_verified) {
            m_client->discardReceipt(m_verified->receipt);
        }
        const auto ids = m_apiIds.keys();
        for (const auto id : ids) {
            m_client->cancel(id);
        }
        if (m_smsApiId != 0) {
            m_client->cancel(m_smsApiId);
        }
    }
}

quint64 QrLoginGateway::requestKey(domain::SessionToken context)
{
    return submit(context, ApiOperation::QrKey);
}

quint64 QrLoginGateway::checkStatus(domain::SessionToken context, QString key)
{
    return submit(context, ApiOperation::QrCheck, std::move(key));
}

bool QrLoginGateway::synchronize(domain::SessionToken context)
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_client && m_client->advanceTo(context);
}

quint64 QrLoginGateway::verifyIdentity(domain::SessionToken context)
{
    return submit(context, ApiOperation::AccountIdentity);
}

quint64 QrLoginGateway::promoteCredentials(domain::SessionToken attempt,
                                           domain::SessionToken target, QUuid receipt)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = ++m_nextId;
    m_pending.insert(id, {target, ApiOperation::AccountIdentity, 0, std::nullopt, true});
    if (!m_client || !m_client->accepts(attempt) || !m_verified || receipt.isNull() ||
        m_verified->receipt != receipt || m_authorizedContext != attempt) {
        rejectLater(id, {AuthError::CredentialRejected});
        return id;
    }
    if (m_promotionId != 0 || !m_apiIds.isEmpty()) {
        rejectLater(id, {AuthError::Busy});
        return id;
    }
    m_promotionId = id;
    const auto identity = m_verified->identity;
    QTimer::singleShot(0, this, [this, id, attempt, target, receipt, identity] {
        const auto it = m_pending.constFind(id);
        if (it == m_pending.constEnd()) {
            return;
        }
        if (it->failure) {
            finishFailure(id, *it->failure);
            return;
        }
        if (!m_client || !m_verified || m_verified->receipt != receipt ||
            !m_client->accepts(attempt)) {
            finishFailure(id, {AuthError::StaleContext});
            return;
        }
        m_verified.reset();
        if (!m_client->promoteReceipt(attempt, target, receipt)) {
            finishFailure(id, {AuthError::CredentialRejected});
            return;
        }
        m_pending.remove(id);
        m_promotionId = 0;
        m_boundContext = target;
        m_boundAccount = identity.accountId;
        emit promotionCompleted(id, target, identity);
    });
    return id;
}

quint64 QrLoginGateway::submit(domain::SessionToken context, ApiOperation operation, QString key)
{
    QJsonObject parameters;
    if (operation == ApiOperation::QrCheck) {
        parameters.insert(QStringLiteral("key"), key);
    }
    return submitWith(context, operation, std::move(parameters));
}

quint64 QrLoginGateway::requestCredentialLogin(domain::SessionToken context,
                                               domain::CredentialLogin login)
{
    QJsonObject parameters;
    auto operation = ApiOperation::CellphoneLogin;
    switch (login.method) {
    case domain::CredentialMethod::PhoneCode:
    case domain::CredentialMethod::PhonePassword:
        parameters.insert(QStringLiteral("countrycode"), login.countryCode);
        parameters.insert(QStringLiteral("phone"), login.account);
        parameters.insert(login.method == domain::CredentialMethod::PhoneCode
                              ? QStringLiteral("captcha")
                              : QStringLiteral("password"),
                          login.secret);
        break;
    case domain::CredentialMethod::EmailPassword:
        operation = ApiOperation::EmailLogin;
        parameters.insert(QStringLiteral("username"), login.account);
        parameters.insert(QStringLiteral("password"), login.secret);
        break;
    }
    // Validity is checked in submitWith (operationPayload rejects malformed input too).
    if (!domain::validCredentialLogin(login)) {
        parameters = {};
    }
    return submitWith(context, operation, std::move(parameters));
}

quint64 QrLoginGateway::sendSmsCode(domain::SessionToken context, domain::SmsCodeRequest request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, ApiOperation::SmsCodeSend});
    if (!m_client) {
        rejectLater(id, {AuthError::Network});
    } else if (!m_client->accepts(context)) {
        rejectLater(id, {AuthError::StaleContext});
    } else if (m_smsId != 0) {
        rejectLater(id, {AuthError::Busy});
    } else if (!domain::validSmsCodeRequest(request)) {
        rejectLater(id, {AuthError::InvalidRequest});
    } else {
        // Outside the single-flight slot: a QR code being polled keeps working.
        const QJsonObject parameters{{QStringLiteral("ctcode"), request.countryCode},
                                     {QStringLiteral("cellphone"), request.phone}};
        m_smsId = id;
        m_smsApiId = m_client->submit(context, ApiOperation::SmsCodeSend, parameters);
        m_pending[id].apiId = m_smsApiId;
    }
    return id;
}

quint64 QrLoginGateway::submitWith(domain::SessionToken context, ApiOperation operation,
                                   QJsonObject parameters)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, operation});
    const bool credential =
        operation == ApiOperation::CellphoneLogin || operation == ApiOperation::EmailLogin;
    if (!m_client) {
        rejectLater(id, {AuthError::Network});
    } else if (!m_client->accepts(context)) {
        rejectLater(id, {AuthError::StaleContext});
    } else if (!m_apiIds.isEmpty() || m_promotionId != 0 || m_restoreId != 0) {
        rejectLater(id, {AuthError::Busy});
    } else if ((operation == ApiOperation::QrKey && m_keyContext == context) ||
               (operation == ApiOperation::QrCheck &&
                (context != m_keyContext ||
                 parameters.value(QStringLiteral("key")).toString() != m_key ||
                 !validQrKey(m_key))) ||
               (operation == ApiOperation::AccountIdentity &&
                (context != m_authorizedContext || m_verified.has_value())) ||
               // One sign-in per attempt token: never after a QR key or an authorization.
               (credential && (m_keyContext == context || m_authorizedContext == context ||
                               m_verified.has_value())) ||
               operation == ApiOperation::SmsCodeSend || (credential && parameters.isEmpty())) {
        rejectLater(id, {AuthError::InvalidRequest});
    } else {
        const auto apiId = m_client->submit(context, operation, parameters);
        m_pending[id].apiId = apiId;
        m_apiIds.insert(apiId, id);
    }
    return id;
}

void QrLoginGateway::rejectLater(quint64 id, AuthFailure failure)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return;
    }
    it->failure = failure;
    QTimer::singleShot(0, this, [this, id] {
        const auto it = m_pending.constFind(id);
        if (it != m_pending.constEnd() && it->failure) {
            finishFailure(id, *it->failure);
        }
    });
}

void QrLoginGateway::finishFailure(quint64 id, AuthFailure failure)
{
    if (!m_pending.contains(id)) {
        return;
    }
    const auto pending = m_pending.take(id);
    m_apiIds.remove(pending.apiId);
    if (pending.failure) {
        failure = *pending.failure;
    }
    if (m_client && !pending.promotion && !m_client->accepts(pending.context)) {
        failure = {AuthError::StaleContext};
    }
    if (id == m_restoreId) {
        m_restoreId = 0;
    }
    if (id == m_promotionId) {
        m_promotionId = 0;
    }
    if (id == m_smsId) {
        m_smsId = 0;
        m_smsApiId = 0;
    }
    if (pending.promotion) {
        emit promotionCompleted(id, pending.context, failure);
    } else if (pending.operation == ApiOperation::AccountIdentity) {
        emit identityCompleted(id, pending.context, failure);
    } else if (pending.operation == ApiOperation::QrKey) {
        emit keyCompleted(id, pending.context, failure);
    } else if (pending.operation == ApiOperation::CellphoneLogin ||
               pending.operation == ApiOperation::EmailLogin) {
        emit credentialLoginCompleted(id, pending.context, failure);
    } else if (pending.operation == ApiOperation::SmsCodeSend) {
        emit smsCodeCompleted(id, pending.context, failure);
    } else {
        emit statusCompleted(id, pending.context, failure);
    }
}

void QrLoginGateway::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto it = m_pending.constFind(id);
    if (it == m_pending.constEnd()) {
        return;
    }
    if (it->apiId != 0 && m_client) {
        m_client->cancel(it->apiId);
    } else {
        rejectLater(id, {AuthError::Canceled});
    }
}

void QrLoginGateway::receive(quint64 apiId, domain::SessionToken context, const ApiResult& result)
{
    const auto id = apiId != 0 && apiId == m_smsApiId ? m_smsId : m_apiIds.value(apiId);
    const auto it = m_pending.constFind(id);
    if (it == m_pending.constEnd()) {
        return;
    }
    if (context != it->context || !m_client || !m_client->accepts(context)) {
        finishFailure(id, {AuthError::StaleContext});
        return;
    }
    if (result.error != ApiError::None) {
        finishFailure(id, failureFor(result));
        return;
    }
    const auto code = result.decoded.businessCode;
    if (!code) {
        finishFailure(id, {AuthError::InvalidResponse});
        return;
    }
    if (it->operation == ApiOperation::AccountIdentity) {
        const auto identity =
            *code == 200 ? mapAccountIdentity(result.decoded.object) : std::nullopt;
        if (!it->restoredAccount.isEmpty() &&
            (!identity || identity->accountId != it->restoredAccount ||
             result.credentialReceipt.isNull())) {
            m_client->discardReceipt(result.credentialReceipt);
            if (m_vault && (*code == 200 || *code == 301 || *code == 302)) {
                m_vault->forget(context);
            }
            finishFailure(id, {AuthError::CredentialRejected});
            return;
        }
        if (!identity || result.credentialReceipt.isNull()) {
            m_client->discardReceipt(result.credentialReceipt);
            finishFailure(id, *code == 200 ? AuthFailure{AuthError::InvalidResponse, code}
                                           : failureFor(result));
            return;
        }
        m_verified = application::VerifiedLogin{*identity, result.credentialReceipt};
        m_restoreId = 0;
        m_apiIds.remove(apiId);
        m_pending.remove(id);
        emit identityCompleted(id, context, *m_verified);
        return;
    }
    if (it->operation == ApiOperation::CellphoneLogin ||
        it->operation == ApiOperation::EmailLogin) {
        if (*code != 200) {
            finishFailure(id, failureFor(result)); // providerCode tells the UI why
            return;
        }
        m_authorizedContext = context; // only enables identity verification, not login
        m_apiIds.remove(apiId);
        m_pending.remove(id);
        emit credentialLoginCompleted(id, context, true);
        return;
    }
    if (it->operation == ApiOperation::SmsCodeSend) {
        if (*code != 200) {
            finishFailure(id, failureFor(result));
            return;
        }
        m_smsId = 0;
        m_smsApiId = 0;
        m_pending.remove(id);
        emit smsCodeCompleted(id, context, true);
        return;
    }
    if (it->operation == ApiOperation::QrKey) {
        if (*code != 200) {
            finishFailure(id, failureFor(result));
            return;
        }
        const auto key = result.decoded.object.value(QStringLiteral("unikey")).toString();
        if (!validQrKey(key)) {
            finishFailure(id, {AuthError::InvalidResponse, code});
            return;
        }
        m_key = key;
        m_keyContext = context;
        QUrl url(QStringLiteral("https://music.163.com/login"));
        url.setQuery(QString::fromLatin1("codekey=" + QUrl::toPercentEncoding(key)));
        m_apiIds.remove(apiId);
        m_pending.remove(id);
        emit keyCompleted(id, context, domain::QrLoginChallenge{key, url});
        return;
    }
    std::optional<domain::QrLoginStatus> state;
    switch (*code) {
    case 800:
        state = domain::QrLoginStatus::Expired;
        break;
    case 801:
        state = domain::QrLoginStatus::WaitingForScan;
        break;
    case 802:
        state = domain::QrLoginStatus::WaitingForConfirmation;
        break;
    case 803:
        m_authorizedContext = context;
        state = domain::QrLoginStatus::Authorized;
        break;
    default:
        finishFailure(id, failureFor(result));
        return;
    }
    if (*state == domain::QrLoginStatus::Expired || *state == domain::QrLoginStatus::Authorized) {
        m_key.clear(); // terminal QR state; a new challenge requires a new epoch
    }
    m_apiIds.remove(apiId);
    m_pending.remove(id);
    emit statusCompleted(id, context, *state);
}

} // namespace cirrus::netease
