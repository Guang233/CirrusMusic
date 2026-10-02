#include "lyric_timeline.h"
#include <QSet>
#include <algorithm>
#include <limits>

namespace cirrus::application
{
using namespace domain;
namespace
{
constexpr qint64 MaxTime = 7LL * 24 * 60 * 60 * 1000;
}
LyricTimeline::Track LyricTimeline::index(const LyricTrack& source)
{
    Track result;
    if (source.availability != LyricAvailability::Present ||
        source.timing == LyricTiming::PlainText || source.lines.size() > 10000 ||
        source.providerOffsetMs < -MaxTime || source.providerOffsetMs > MaxTime)
        return result;
    result.offset = source.providerOffsetMs;
    QList<qint64> starts;
    for (const auto& line : source.lines)
        if (line.startMs && *line.startMs >= 0 && *line.startMs <= MaxTime)
            starts.append(*line.startMs);
    std::sort(starts.begin(), starts.end());
    qsizetype wordCount = 0;
    for (qsizetype i = 0; i < source.lines.size(); ++i) {
        const auto& line = source.lines[i];
        if (!line.startMs || *line.startMs < 0 || *line.startMs > MaxTime)
            continue;
        const auto start = *line.startMs;
        const auto next = std::upper_bound(starts.cbegin(), starts.cend(), start);
        const auto end =
            line.endMs.value_or(next == starts.cend() ? std::numeric_limits<qint64>::max() : *next);
        if (end <= start)
            continue;
        result.lines.append({start, end, 0, i, -1});
        if (source.timing != LyricTiming::Words)
            continue;
        if (line.words.size() > 30000 - wordCount)
            return {};
        wordCount += line.words.size();
        for (qsizetype w = 0; w < line.words.size(); ++w) {
            const auto& word = line.words[w];
            if (word.startMs < 0 || word.startMs > MaxTime || word.durationMs <= 0 ||
                word.durationMs > MaxTime)
                continue;
            result.words.append({word.startMs, word.startMs + word.durationMs, 0, i, w});
        }
    }
    const auto finish = [](QList<Interval>& intervals) {
        std::stable_sort(intervals.begin(), intervals.end(),
                         [](const auto& a, const auto& b) { return a.start < b.start; });
        qint64 maximum = 0;
        for (auto& interval : intervals) {
            maximum = qMax(maximum, interval.end);
            interval.prefixEnd = maximum;
        }
    };
    finish(result.lines);
    finish(result.words);
    return result;
}
void LyricTimeline::reset(const SongLyrics& lyrics)
{
    m_tracks = {index(lyrics.original.standard),     index(lyrics.original.enhanced),
                index(lyrics.translation.standard),  index(lyrics.translation.enhanced),
                index(lyrics.romanization.standard), index(lyrics.romanization.enhanced)};
}
void LyricTimeline::clear() { m_tracks = {}; }
LyricCursor LyricTimeline::locate(const Track& track, qint64 position, qint64 userOffset)
{
    LyricCursor cursor;
    const auto time = position - track.offset - userOffset;
    if (time < 0)
        return cursor;
    QSet<qsizetype> active;
    const auto visit = [time](const QList<Interval>& intervals, const auto& consume) {
        auto i = std::upper_bound(
                     intervals.cbegin(), intervals.cend(), time,
                     [](qint64 t, const Interval& interval) { return t < interval.start; }) -
                 intervals.cbegin();
        while (i > 0) {
            const auto& interval = intervals[--i];
            if (interval.prefixEnd <= time)
                break;
            if (interval.end > time)
                consume(interval);
        }
    };
    visit(track.lines, [&](const Interval& line) {
        cursor.lines.append(line.line);
        active.insert(line.line);
    });
    visit(track.words, [&](const Interval& word) {
        if (active.contains(word.line))
            cursor.words.append({word.line, word.word});
    });
    std::sort(cursor.lines.begin(), cursor.lines.end());
    std::sort(cursor.words.begin(), cursor.words.end(), [](const auto& a, const auto& b) {
        return a.line < b.line || (a.line == b.line && a.word < b.word);
    });
    return cursor;
}
LyricCursors LyricTimeline::at(qint64 position, qint64 userOffset) const
{
    if (position < 0 || position > MaxTime || userOffset < -120000 || userOffset > 120000)
        return {};
    return {{locate(m_tracks[0], position, userOffset), locate(m_tracks[1], position, userOffset)},
            {locate(m_tracks[2], position, userOffset), locate(m_tracks[3], position, userOffset)},
            {locate(m_tracks[4], position, userOffset), locate(m_tracks[5], position, userOffset)}};
}
} // namespace cirrus::application
