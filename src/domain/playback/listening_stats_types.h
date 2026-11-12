#pragma once

#include "listening_types.h"

#include <QDate>

#include <array>

// Local listening statistics over the stored history (docs/api/local-listening-history.md
// §6). Values only; the store computes them in SQL on the database thread.
namespace cirrus::domain
{
// A record is one play when it is not a preview and either 30 s were heard, or the song is
// shorter than that and was heard to its end (or 90 % of it). Every record, previews and
// skips too, adds its time.
inline constexpr qint64 CountedPlayMinimumMs = 30000;

enum class ListeningPeriodSize { Day, Week, Month }; // local calendar; weeks start on Monday
enum class ListeningRankOrder { Plays, Time };
CIRRUS_LOG_ENUM(ListeningPeriodSize, Day, Week, Month)
CIRRUS_LOG_ENUM(ListeningRankOrder, Plays, Time)

struct ListeningStatsQuery {
    qint64 fromMs = 0; // UTC, inclusive; 0: from the first record
    qint64 toMs = 0;   // UTC, exclusive; 0: no end
    qint64 nowMs = 0;  // "today" for the current streak; 0: the clock
    ListeningPeriodSize periodSize = ListeningPeriodSize::Day;
    ListeningRankOrder order = ListeningRankOrder::Plays;
    int limit = 10; // per ranking, 1..100
    [[nodiscard]] bool isValid() const
    {
        constexpr qint64 last = 253402300800000LL; // 10000-01-01T00:00:00Z
        return fromMs >= 0 && fromMs < last && toMs >= 0 && toMs <= last &&
               (!toMs || toMs > fromMs) && nowMs >= 0 && nowMs < last &&
               periodSize >= ListeningPeriodSize::Day && periodSize <= ListeningPeriodSize::Month &&
               order >= ListeningRankOrder::Plays && order <= ListeningRankOrder::Time &&
               limit >= 1 && limit <= 100;
    }
};

struct ListeningTotals {
    qint64 playedMs = 0; // every record in the range
    int plays = 0;       // counted plays
    int records = 0;     // loads that were heard at all
    int songs = 0;       // distinct songs with a counted play
    int artists = 0;     // ... their distinct artists (songs with details only)
    int albums = 0;      // ... their distinct albums (songs with details only)
    int newSongs = 0;    // songs whose first counted play ever is in the range
    int days = 0;        // local days with any record
};
struct ListeningPeriod {
    QDate start; // first local day of the day/week/month
    qint64 playedMs = 0;
    int plays = 0;
};
struct ListeningSlot {
    qint64 playedMs = 0;
    int plays = 0;
};
// Song details are the snapshot taken when it was first heard (filled in the background;
// `known` is false until then, and for songs the service no longer has).
struct StatsSong {
    Song song; // id always; title, artists, album and duration when known
    bool known = false;
    int plays = 0;
    qint64 playedMs = 0;
    qint64 lastPlayedAtMs = 0;
};
struct StatsArtist {
    ArtistCredit artist;
    int plays = 0;
    qint64 playedMs = 0; // a song's full time goes to each of its artists
    int songs = 0;
};
struct StatsAlbum {
    AlbumReference album;
    int plays = 0;
    qint64 playedMs = 0;
    int songs = 0;
};
struct StatsOrigin {
    ListeningMode mode = ListeningMode::Queue; // PersonalFm: one row, origin None
    PlayOrigin origin;                         // Queue rows have an origin other than None
    int plays = 0;
    qint64 playedMs = 0;
};
struct ListeningStreak {
    int current = 0; // consecutive days with a counted play, ending today or yesterday
    int longest = 0;
    QDate longestEnd;
    QDate lastDay; // latest day with a counted play
};
struct ListeningStats {
    ListeningStatsQuery query;
    ListeningTotals totals;
    QList<ListeningPeriod> periods;          // periods with records, oldest first
    std::array<ListeningSlot, 24> hours{};   // local hour the record started
    std::array<ListeningSlot, 7> weekdays{}; // Monday first
    // Both at once: [weekday * 24 + hour], Monday first. hours and weekdays are its sums.
    std::array<ListeningSlot, 7 * 24> weekHours{};
    QList<StatsSong> songs;
    QList<StatsArtist> artists;
    QList<StatsAlbum> albums;
    QList<StatsOrigin> origins;
    ListeningStreak streak;                // all time, not the range
    std::optional<qint64> firstRecordAtMs; // all time
    int songsWithoutDetails = 0;           // in the range, still being filled in
};
} // namespace cirrus::domain

Q_DECLARE_METATYPE(cirrus::domain::ListeningStats)
