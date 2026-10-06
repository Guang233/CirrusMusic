#include "collection_gateway.h"
#include "domain/music/collection_types.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <QThread>
#include <QTimer>
#include <type_traits>
#include <utility>
namespace cirrus::netease
{
using namespace domain;
std::optional<CollectionRequest> collectionRequest(const CollectionMutation& mutation)
{
    if (!mutation.isValid())
        return std::nullopt;
    return std::visit(
        [](const auto& c) -> CollectionRequest {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, SongHeart>)
                return {ApiOperation::SongHeart,
                        {{QStringLiteral("id"), c.song.value}, {QStringLiteral("liked"), c.liked}}};
            else if constexpr (std::is_same_v<T, PlaylistCollection>) {
                QJsonArray ids;
                for (const auto& id : c.songs)
                    ids.append(id.value);
                return {ApiOperation::PlaylistCollectSongs,
                        {{QStringLiteral("pid"), c.playlist.value},
                         {QStringLiteral("ids"), ids},
                         {QStringLiteral("op"), c.action == CollectionAction::Collect
                                                    ? QStringLiteral("add")
                                                    : QStringLiteral("del")}}};
            } else if constexpr (std::is_same_v<T, AlbumCollection>)
                return {c.action == CollectionAction::Collect ? ApiOperation::AlbumSubscribe
                                                              : ApiOperation::AlbumUnsubscribe,
                        {{QStringLiteral("id"), c.album.value}}};
            else if constexpr (std::is_same_v<T, ArtistCollection>)
                return {c.action == CollectionAction::Collect ? ApiOperation::ArtistSubscribe
                                                              : ApiOperation::ArtistUnsubscribe,
                        {{QStringLiteral("id"), c.artist.value}}};
            else if constexpr (std::is_same_v<T, PodcastSubscription>)
                return {c.action == CollectionAction::Collect ? ApiOperation::PodcastSubscribe
                                                              : ApiOperation::PodcastUnsubscribe,
                        {{QStringLiteral("id"), c.podcast.value}}};
            else if constexpr (std::is_same_v<T, PlaylistCreate>)
                return {ApiOperation::PlaylistCreate,
                        {{QStringLiteral("name"), c.name},
                         {QStringLiteral("privacy"), c.privacy == PlaylistPrivacy::Private
                                                         ? QStringLiteral("10")
                                                         : QStringLiteral("0")}}};
            else if constexpr (std::is_same_v<T, PlaylistRename>)
                return {
                    ApiOperation::PlaylistRename,
                    {{QStringLiteral("id"), c.playlist.value}, {QStringLiteral("name"), c.name}}};
            else if constexpr (std::is_same_v<T, PlaylistDescription>)
                return {ApiOperation::PlaylistDescription,
                        {{QStringLiteral("id"), c.playlist.value},
                         {QStringLiteral("desc"), c.description}}};
            else if constexpr (std::is_same_v<T, PlaylistDelete>)
                return {ApiOperation::PlaylistDelete, {{QStringLiteral("id"), c.playlist.value}}};
            else {
                QJsonArray ids;
                for (const auto& id : c.ids())
                    ids.append(id.value);
                if constexpr (std::is_same_v<T, PlaylistSongOrder>)
                    return {ApiOperation::PlaylistSongOrder,
                            {{QStringLiteral("pid"), c.playlist().value},
                             {QStringLiteral("ids"), ids}}};
                else
                    return {ApiOperation::AccountPlaylistOrder, {{QStringLiteral("ids"), ids}}};
            }
        },
        mutation.change);
}
CollectionResult collectionResult(const ApiResult& api)
{
    CollectionResult result;
    result.outcome =
        api.submittedToTransport ? CollectionOutcome::Unknown : CollectionOutcome::NotSent;
    if (api.error == ApiError::None && api.decoded.ok() && api.decoded.businessCode) {
        result.providerCode = api.decoded.businessCode;
        result.outcome = *api.decoded.businessCode == 200 ? CollectionOutcome::Acknowledged
                                                          : CollectionOutcome::Rejected;
        result.error = *api.decoded.businessCode == 200 ? CollectionError::None
                                                        : CollectionError::ProviderRejected;
    } else if (api.error == ApiError::Canceled)
        result.error = CollectionError::Canceled;
    else if (api.error == ApiError::StaleContext)
        result.error = CollectionError::StaleContext;
    else if (api.error == ApiError::Busy)
        result.error = CollectionError::Busy;
    else if (api.error == ApiError::InvalidRequest)
        result.error = CollectionError::InvalidRequest;
    else if (api.error == ApiError::Cookie)
        result.error = CollectionError::AuthenticationRequired;
    else if (api.error == ApiError::Decode || api.error == ApiError::None)
        result.error = CollectionError::InvalidResponse;
    else if (api.transportError == TransportError::Timeout)
        result.error = CollectionError::Timeout;
    else if (api.error == ApiError::Transport)
        result.error = CollectionError::Network;
    else
        result.error = CollectionError::ProtocolFailure;
    if (api.httpStatus == 429 || api.decoded.businessCode == 429) {
        result.error = CollectionError::RateLimited;
        bool ok = false;
        const auto seconds = api.retryAfter.toLongLong(&ok);
        if (ok && seconds >= 0 && seconds <= 86400)
            result.retryAfterMs = seconds * 1000;
    }
    return result;
}
CollectionResult collectionResult(const ApiResult& api, const CollectionMutation& mutation)
{
    auto result = collectionResult(api);
    if (result.outcome != CollectionOutcome::Acknowledged ||
        !std::holds_alternative<PlaylistCreate>(mutation.change))
        return result;
    // Candidate schema only: a response code alone cannot identify a created resource.
    const auto root = api.decoded.object;
    const auto playlist = root.value(QStringLiteral("playlist"));
    const auto id = playlist.isObject()
                        ? mapping::providerId(playlist.toObject().value(QStringLiteral("id")))
                        : QString{};
    const auto echo = root.value(QStringLiteral("id"));
    if (id.isEmpty() || (!echo.isUndefined() && mapping::providerId(echo) != id)) {
        result.outcome = CollectionOutcome::Unknown;
        result.error = CollectionError::InvalidResponse;
    } else {
        result.createdPlaylist = PlaylistId{id};
    }
    return result;
}
CollectionGateway::CollectionGateway(application::SessionCoordinator& session,
                                     SessionApiClient& client, QObject* parent)
    : ICollectionGateway(parent), m_session(session), m_client(&client)
{
    Q_ASSERT(session.thread() == thread() && client.thread() == thread());
    connect(&client, &SessionApiClient::completed, this,
            [this](quint64 apiId, SessionToken token, const ApiResult& api) {
                const auto id = m_ids.value(apiId);
                if (!m_pending.contains(id))
                    return;
                const auto& p = m_pending[id];
                auto result = collectionResult(api, p.mutation);
                if (p.context != token || !m_session.accepts(p.context) ||
                    m_session.snapshot().phase != SessionPhase::Authenticated ||
                    m_session.snapshot().accountId != p.accountId) {
                    result.outcome = api.submittedToTransport ? CollectionOutcome::Unknown
                                                              : CollectionOutcome::NotSent;
                    result.error = CollectionError::StaleContext;
                    result.providerCode.reset();
                    result.retryAfterMs.reset();
                    result.createdPlaylist.reset();
                }
                finish(id, result);
            });
    connect(&session, &application::SessionCoordinator::changed, this, [this](SessionSnapshot s) {
        if (!m_client)
            return;
        const auto pending = m_pending;
        for (const auto& p : pending)
            if (m_client && (p.context != s.token || s.phase != SessionPhase::Authenticated ||
                             p.accountId != s.accountId))
                m_client->cancel(p.apiId);
    });
    connect(&client, &QObject::destroyed, this, [this] {
        for (const auto id : m_pending.keys())
            finish(id,
                   {{},
                    m_pending[id].apiId ? CollectionOutcome::Unknown : CollectionOutcome::NotSent,
                    CollectionError::Network});
    });
}
CollectionGateway::~CollectionGateway()
{
    if (!m_client)
        return;
    disconnect(m_client, nullptr, this, nullptr);
    for (const auto id : m_ids.keys())
        m_client->cancel(id);
}
quint64 CollectionGateway::submit(SessionToken context, QString accountId,
                                  CollectionMutation mutation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 8)
        return 0;
    for (const auto& p : std::as_const(m_pending))
        if (p.mutation.eventId == mutation.eventId && !mutation.eventId.isNull())
            return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, accountId, mutation});
    const auto snapshot = m_session.snapshot();
    auto error = CollectionError::None;
    const auto sourceContext = collectionSourceContext(mutation);
    const auto sourceAccount = collectionSourceAccount(mutation);
    if (!m_session.accepts(context) || (sourceContext && *sourceContext != context))
        error = CollectionError::StaleContext;
    else if (snapshot.phase != SessionPhase::Authenticated)
        error = CollectionError::AuthenticationRequired;
    else if (!mutation.isValid() || accountId != snapshot.accountId || !validMusicId(accountId) ||
             (sourceAccount && *sourceAccount != accountId))
        error = CollectionError::InvalidRequest;
    else if (!m_client)
        error = CollectionError::Network;
    if (error != CollectionError::None) {
        QTimer::singleShot(
            0, this, [this, id, error] { finish(id, {{}, CollectionOutcome::NotSent, error}); });
        return id;
    }
    const auto request = collectionRequest(mutation);
    const auto apiId = m_client->submit(context, request->operation, request->parameters);
    m_pending[id].apiId = apiId;
    m_ids.insert(apiId, id);
    return id;
}
void CollectionGateway::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.value(id);
    if (p.apiId && m_client)
        m_client->cancel(p.apiId);
    else
        QTimer::singleShot(0, this, [this, id] {
            finish(id, {{}, CollectionOutcome::NotSent, CollectionError::Canceled});
        });
}
void CollectionGateway::finish(quint64 id, CollectionResult result)
{
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.take(id);
    m_ids.remove(p.apiId);
    result.eventId = p.mutation.eventId;
    emit completed(id, p.context, result);
}
} // namespace cirrus::netease
