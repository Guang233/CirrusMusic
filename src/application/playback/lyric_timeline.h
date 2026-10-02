#pragma once
#include "domain/playback/listening_types.h"
#include <array>

namespace cirrus::application
{
// Pure Core time index. No wall-clock extrapolation, cross-language alignment or synthesized words.
class LyricTimeline
{
  public:
    void reset(const domain::SongLyrics& lyrics);
    void clear();
    [[nodiscard]] domain::LyricCursors at(qint64 songPositionMs, qint64 userOffsetMs = 0) const;

  private:
    struct Interval {
        qint64 start, end, prefixEnd;
        qsizetype line, word;
    };
    struct Track {
        QList<Interval> lines, words;
        qint64 offset = 0;
    };
    static Track index(const domain::LyricTrack& source);
    static domain::LyricCursor locate(const Track& track, qint64 position, qint64 userOffset);
    std::array<Track, 6> m_tracks;
};
} // namespace cirrus::application
