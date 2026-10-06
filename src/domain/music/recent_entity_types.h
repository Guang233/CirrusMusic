#pragma once
#include "entity_search_types.h"
namespace cirrus::domain
{
enum class RecentEntityKind { Playlist, Album, Podcast, Voice };
struct RecentEntitiesQuery {
    QString accountId;
    RecentEntityKind kind = RecentEntityKind::Playlist;
    int limit = 100;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(accountId) && limit >= 1 && limit <= 100 &&
               (kind == RecentEntityKind::Playlist || kind == RecentEntityKind::Album ||
                kind == RecentEntityKind::Podcast || kind == RecentEntityKind::Voice);
    }
};
template <class T> struct RecentEntity {
    T entity;
    std::optional<qint64> providerPlayTime; // raw provider value, NOT duration/clock guarantee
};
template <class T> struct CloudRecentEntities {
    RecentEntitiesQuery query;
    QList<RecentEntity<T>> items;
    std::optional<qint64> total; // no offset/more/fullness inference
};
using CloudRecentPlaylists = CloudRecentEntities<DiscoveryPlaylist>;
using CloudRecentAlbums = CloudRecentEntities<Album>;
using CloudRecentPodcasts = CloudRecentEntities<Podcast>;
using CloudRecentVoices = CloudRecentEntities<Voice>;
} // namespace cirrus::domain
