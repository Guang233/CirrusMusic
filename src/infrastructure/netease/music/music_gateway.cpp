#include "music_gateway.h"
#include "music_mapper.h"

#include <QJsonArray>
#include <QThread>
#include <QTimer>

namespace cirrus::netease
{
using namespace application;
namespace
{
MusicFailure failure(const ApiResult& result)
{
    MusicFailure error{MusicError::ProtocolFailure};
    if (result.error == ApiError::StaleContext)
        error.error = MusicError::StaleContext;
    else if (result.error == ApiError::Canceled ||
             result.transportError == TransportError::Canceled)
        error.error = MusicError::Canceled;
    else if (result.error == ApiError::Busy)
        error.error = MusicError::Busy;
    else if (result.error == ApiError::InvalidRequest)
        error.error = MusicError::InvalidRequest;
    else if (result.error == ApiError::Decode)
        error.error = MusicError::InvalidResponse;
    else if (result.transportError == TransportError::Timeout)
        error.error = MusicError::Timeout;
    else if (result.httpStatus == 429 || result.decoded.businessCode == 429) {
        error.error = MusicError::RateLimited;
        bool ok = false;
        const auto seconds = result.retryAfter.toLongLong(&ok);
        if (ok && seconds >= 0 && seconds <= 86400)
            error.retryAfterMs = seconds * 1000;
    } else if (result.error == ApiError::Transport)
        error.error = MusicError::Network;
    else if (result.decoded.businessCode)
        error.error = MusicError::ProviderRejected;
    else if (result.error == ApiError::None)
        error.error = MusicError::InvalidResponse;
    error.providerCode = result.decoded.businessCode;
    return error;
}
} // namespace

MusicGateway::MusicGateway(SessionApiClient& client, QObject* parent)
    : IMusicGateway(parent), m_client(&client)
{
    Q_ASSERT(client.thread() == thread());
    connect(&client, &SessionApiClient::completed, this,
            [this](quint64 apiId, domain::SessionToken token, const ApiResult& result) {
                const auto id = m_apiIds.value(apiId);
                const auto it = m_pending.constFind(id);
                if (it == m_pending.cend())
                    return;
                if (it->context != token || !m_client || !m_client->accepts(token))
                    finish(id, MusicFailure{MusicError::StaleContext});
                else if (result.error != ApiError::None || result.decoded.businessCode != 200)
                    finish(id, failure(result));
                else
                    finish(id, mapMusicResponse(it->query, result.decoded.object));
            });
    connect(&client, &QObject::destroyed, this, [this] {
        const auto ids = m_pending.keys();
        for (auto id : ids)
            finish(id, MusicFailure{MusicError::Network});
    });
}

MusicGateway::~MusicGateway()
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        for (auto id : m_apiIds.keys())
            m_client->cancel(id);
    }
}

quint64 MusicGateway::request(domain::SessionToken context, MusicQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = ++m_nextId;
    const bool busy = m_pending.size() >= 16;
    m_pending.insert(id, {context, query});
    if (!m_client || busy || !validMusicQuery(query)) {
        const auto error = busy        ? MusicError::Busy
                           : !m_client ? MusicError::Network
                                       : MusicError::InvalidRequest;
        QTimer::singleShot(0, this, [this, id, error] { finish(id, MusicFailure{error}); });
        return id;
    }
    ApiOperation operation;
    QJsonObject parameters;
    if (const auto* search = std::get_if<domain::SongSearch>(&query)) {
        operation = ApiOperation::SearchSongs;
        parameters = {{QStringLiteral("keywords"), search->keywords},
                      {QStringLiteral("limit"), search->limit},
                      {QStringLiteral("offset"), search->offset}};
    } else if (const auto* details = std::get_if<DetailQuery>(&query)) {
        operation = ApiOperation::SongDetails;
        QJsonArray ids;
        for (const auto& song : details->songs)
            ids.append(song.value);
        parameters.insert(QStringLiteral("ids"), ids);
    } else {
        operation = ApiOperation::SongLyrics;
        parameters.insert(QStringLiteral("id"), std::get<LyricQuery>(query).song.value);
    }
    const auto apiId = m_client->submit(context, operation, parameters);
    m_pending[id].apiId = apiId;
    m_apiIds.insert(apiId, id);
    return id;
}

void MusicGateway::cancel(quint64 id)
{
    const auto it = m_pending.constFind(id);
    if (it == m_pending.cend())
        return;
    if (m_client && it->apiId != 0)
        m_client->cancel(it->apiId);
    QTimer::singleShot(0, this, [this, id] { finish(id, MusicFailure{MusicError::Canceled}); });
}

void MusicGateway::finish(quint64 id, MusicResult result)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    m_apiIds.remove(pending.apiId);
    if (m_client && !m_client->accepts(pending.context))
        result = MusicFailure{MusicError::StaleContext};
    emit completed(id, pending.context, std::move(result));
}
} // namespace cirrus::netease
