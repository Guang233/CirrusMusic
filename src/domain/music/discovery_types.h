#pragma once
#include "music_types.h"
#include <QDateTime>

namespace cirrus::domain
{
// Current server recommendations only; no caller-controlled date or forced afresh.
struct DailyRecommendationsQuery {
    [[nodiscard]] bool isValid() const { return true; }
};
struct PersonalFmQuery {
    [[nodiscard]] bool isValid() const { return true; }
};
struct RecommendedSong {
    Song song;
    std::optional<QString> inlineReason;
};
struct RecommendationReason {
    SongId song;
    QString text;
};
struct DailyRecommendations {
    QList<RecommendedSong> songs;
    QList<RecommendationReason> reasons; // keyed by SongId, never joined by array index
    QDateTime receivedAtUtc; // local receipt time, NOT the provider's recommendation day
};
struct PersonalFmBatch {
    QList<Song> songs; // one bounded dynamic batch; not a playlist or a pagination result
};
} // namespace cirrus::domain
