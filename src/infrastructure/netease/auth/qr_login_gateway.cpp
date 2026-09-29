#include "qr_login_gateway.h"

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
                    // Release the single-flight slot NOW, including pre-dispatch
                    // calls. Old completion still carries its original token.
                    const auto ids = m_pending.keys();
                    for (const auto id : ids) {
                        auto it = m_pending.find(id);
                        if (it != m_pending.end() && it->context != context) {
                            const auto apiId = it->apiId;
                            m_apiIds.remove(apiId);
                            it->apiId = 0;
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
        m_apiIds.clear();
        const auto ids = m_pending.keys();
        for (const auto id : ids) {
            rejectLater(id, {AuthError::Network});
        }
    });
}

QrLoginGateway::~QrLoginGateway()
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        const auto ids = m_apiIds.keys();
        for (const auto id : ids) {
            m_client->cancel(id);
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

quint64 QrLoginGateway::submit(domain::SessionToken context, ApiOperation operation, QString key)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, operation});
    if (!m_client) {
        rejectLater(id, {AuthError::Network});
    } else if (!m_client->accepts(context)) {
        rejectLater(id, {AuthError::StaleContext});
    } else if (!m_apiIds.isEmpty()) {
        rejectLater(id, {AuthError::Busy});
    } else if ((operation == ApiOperation::QrKey && m_keyContext == context) ||
               (operation == ApiOperation::QrCheck &&
                (context != m_keyContext || key != m_key || !validQrKey(key)))) {
        rejectLater(id, {AuthError::InvalidRequest});
    } else {
        QJsonObject parameters;
        if (operation == ApiOperation::QrCheck) {
            parameters.insert(QStringLiteral("key"), key);
        }
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
    if (m_client && !m_client->accepts(pending.context)) {
        failure = {AuthError::StaleContext};
    }
    if (pending.operation == ApiOperation::QrKey) {
        emit keyCompleted(id, pending.context, failure);
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
    const auto id = m_apiIds.value(apiId);
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
