#pragma once

#include "music_types.h"
#include <QByteArray>
#include <variant>

namespace cirrus::domain
{
enum class CacheKind { Song, Lyrics, Cover };
enum class CacheReadMode { CacheOnly, PreferCache, Refresh };
enum class CacheStatus { Hit, Miss, Expired, Stored, Removed, Statistics, Network };
enum class CacheError {
    None,
    Disabled,
    InvalidRequest,
    Busy,
    Canceled,
    StaleContext,
    Storage,
    Corrupt,
    Capacity,
    Unsupported,
    Network,
    Timeout
};
struct CacheUsage {
    qint64 metadataBytes = 0, coverBytes = 0, entries = 0;
    qint64 databaseBytes = 0; // main file only, not WAL/journal/process memory
};
using CacheValue = std::variant<std::monostate, Song, SongLyrics, QByteArray>;
struct CacheResult {
    quint64 id = 0;
    CacheError error = CacheError::None;
    CacheStatus status = CacheStatus::Miss;
    CacheValue value;
    CacheUsage usage;
    bool persisted = false;
};
inline constexpr qint64 MetadataCacheBudget = 128LL * 1024 * 1024;
inline constexpr qint64 CoverCacheBudget = 512LL * 1024 * 1024;
inline constexpr qint64 CacheDayMs = 86400000;
inline qint64 maximumCacheTtl(CacheKind kind)
{
    return kind == CacheKind::Song ? CacheDayMs : 7 * CacheDayMs;
}
inline qsizetype maximumCachePayload(CacheKind kind)
{
    return kind == CacheKind::Song     ? 512 * 1024
           : kind == CacheKind::Lyrics ? 8 * 1024 * 1024
                                       : 4 * 1024 * 1024;
}
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::CacheResult)
