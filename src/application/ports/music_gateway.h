#pragma once

#include "domain/account/session_types.h"
#include "domain/music/music_types.h"

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
    ProtocolFailure
};
struct MusicFailure {
    MusicError error{};
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs;
};
struct DetailQuery {
    QList<domain::SongId> songs;
};
struct LyricQuery {
    domain::SongId song;
};
using MusicQuery = std::variant<domain::SongSearch, DetailQuery, LyricQuery>;
using MusicResult =
    std::variant<domain::SongSearchPage, domain::SongDetails, domain::SongLyrics, MusicFailure>;

inline bool validMusicQuery(const MusicQuery& query)
{
    if (const auto* search = std::get_if<domain::SongSearch>(&query))
        return search->isValid();
    if (const auto* lyric = std::get_if<LyricQuery>(&query))
        return domain::validMusicId(lyric->song.value);
    const auto& ids = std::get<DetailQuery>(query).songs;
    if (ids.isEmpty() || ids.size() > 100)
        return false;
    QStringList seen;
    for (const auto& id : ids) {
        if (!domain::validMusicId(id.value) || seen.contains(id.value))
            return false;
        seen.append(id.value);
    }
    return true;
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
