#include "vocal_spans.h"
#include <QRegularExpression>
#include <algorithm>

namespace cirrus::application
{
using namespace domain;

namespace
{
constexpr qint64 kLongestLineMs = 8000;
constexpr qsizetype kMostSpans = 2000;

// "作词 : …", "Producer：…": a short label and a colon.
bool credit(const QString& text)
{
    static const QRegularExpression label(QStringLiteral("^\\s*[^:：\\s][^:：]{0,11}[:：]"));
    return label.match(text).hasMatch();
}

const LyricTrack* timed(const LyricVersions& versions)
{
    const auto usable = [](const LyricTrack& track) {
        return track.availability == LyricAvailability::Present &&
               track.timing != LyricTiming::PlainText;
    };
    if (usable(versions.enhanced) && versions.enhanced.timing == LyricTiming::Words)
        return &versions.enhanced;
    if (usable(versions.standard))
        return &versions.standard;
    return usable(versions.enhanced) ? &versions.enhanced : nullptr;
}
} // namespace

QList<VocalSpan> vocalSpans(const SongLyrics& lyrics, qint64 offsetMs)
{
    if (lyrics.instrumental.value_or(false))
        return {};
    const LyricTrack* track = timed(lyrics.original);
    if (!track)
        return {};
    struct Line {
        qint64 start = 0;
        std::optional<qint64> end;
        bool sung = false;
    };
    QList<Line> lines;
    for (const LyricLine& line : track->lines) {
        const bool sung = !line.text.trimmed().isEmpty() && !credit(line.text);
        if (!line.words.isEmpty()) {
            qint64 end = line.words.front().startMs;
            for (const LyricWord& word : line.words)
                end = std::max(end, word.startMs + word.durationMs);
            lines.append({line.words.front().startMs, end, sung});
        } else if (line.startMs) {
            lines.append({*line.startMs, line.endMs, sung});
        }
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line& a, const Line& b) { return a.start < b.start; });
    const qint64 shift = track->providerOffsetMs + offsetMs;
    QList<VocalSpan> spans;
    for (qsizetype i = 0; i < lines.size() && spans.size() < kMostSpans; ++i) {
        const Line& line = lines[i];
        if (!line.sung)
            continue;
        qint64 end = line.end.value_or(line.start + kLongestLineMs);
        if (!line.end) {
            for (qsizetype j = i + 1; j < lines.size(); ++j) {
                if (lines[j].start > line.start) {
                    end = std::min(end, lines[j].start);
                    break;
                }
            }
        }
        if (end > line.start)
            spans.append({line.start + shift, end + shift});
    }
    return spans;
}

} // namespace cirrus::application
