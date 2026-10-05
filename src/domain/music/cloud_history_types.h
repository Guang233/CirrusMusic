#pragma once
#include "music_types.h"

namespace cirrus::domain
{
// Current verified account only. No offset exists in the pinned recent-song request.
struct RecentSongsQuery {
    QString accountId;
    int limit = 100;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(accountId) && limit >= 1 && limit <= 100;
    }
};
enum class RankingPeriod { AllTime, LastWeek };
struct ListeningRankingQuery {
    QString accountId;
    RankingPeriod period = RankingPeriod::AllTime;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(accountId) &&
               (period == RankingPeriod::AllTime || period == RankingPeriod::LastWeek);
    }
};
struct RecentSong {
    Song song;
    std::optional<qint64>
        providerPlayTime; // raw playTime; clock/unit semantics unverified, NOT duration
};
struct CloudRecentSongs {
    RecentSongsQuery query;
    QList<RecentSong> songs;     // preserve server order and repeated songs
    std::optional<qint64> total; // never infer from a short list, no fabricated hasMore/offset
};
struct RankedSong {
    Song song;
    std::optional<qint64> playCount;
    std::optional<double> score; // provider score, not counts/seconds/locally derived rank
};
struct ListeningRanking {
    static constexpr qsizetype MaximumSongs = 1000; // reject excess, never silently truncate
    ListeningRankingQuery query;
    QList<RankedSong> songs; // provider order, no local sort/score normalization
};
} // namespace cirrus::domain
