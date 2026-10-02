#pragma once

#include "domain/account/session_types.h"
#include "domain/music/catalog_types.h"
#include "domain/music/discovery_types.h"

#include <QObject>
#include <variant>

namespace cirrus::application
{

enum class MusicError {
    InvalidRequest,
    Busy,
    Canceled,
    StaleContext,
    Network,
    Timeout,
    RateLimited,
    ProviderRejected,
    InvalidResponse,
    ProtocolFailure,
    IncompleteCatalog,
    AuthenticationRequired
};
struct MusicFailure {
    MusicError error{};
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs;
};
struct DetailQuery {
    QList<domain::SongId> songs;
    [[nodiscard]] bool isValid() const
    {
        if (songs.isEmpty() || songs.size() > 100)
            return false;
        QStringList seen;
        for (const auto& id : songs) {
            if (!domain::validMusicId(id.value) || seen.contains(id.value))
                return false;
            seen.append(id.value);
        }
        return true;
    }
};
struct LyricQuery {
    domain::SongId song;
    [[nodiscard]] bool isValid() const { return domain::validMusicId(song.value); }
};
using MusicQuery =
    std::variant<domain::SongSearch, DetailQuery, LyricQuery, domain::AlbumQuery,
                 domain::ArtistQuery, domain::ArtistSongsQuery, domain::ArtistAlbumsQuery,
                 domain::PlaylistQuery, domain::PlaylistSongsQuery,
                 domain::DailyRecommendationsQuery, domain::PersonalFmQuery>;
using MusicResult =
    std::variant<domain::SongSearchPage, domain::SongDetails, domain::SongLyrics, MusicFailure,
                 domain::AlbumDetails, domain::ArtistDetails, domain::ArtistSongsPage,
                 domain::ArtistAlbumsPage, domain::PlaylistDetails, domain::PlaylistSongsPage,
                 domain::DailyRecommendations, domain::PersonalFmBatch>;

inline bool validMusicQuery(const MusicQuery& query)
{
    return std::visit([](const auto& value) { return value.isValid(); }, query);
}

inline bool requiresAuthenticatedMusicQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::DailyRecommendationsQuery>(query) ||
           std::holds_alternative<domain::PersonalFmQuery>(query);
}

// Same-thread, deferred, value-only port; no arbitrary endpoints, JSON or Cookie.
class IMusicGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    [[nodiscard]] virtual quint64 request(domain::SessionToken context, MusicQuery query) = 0;
    virtual void cancel(quint64 id) = 0;
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::application::MusicResult result);
};

} // namespace cirrus::application
Q_DECLARE_METATYPE(cirrus::application::MusicResult)
Q_DECLARE_METATYPE(cirrus::application::MusicFailure)
