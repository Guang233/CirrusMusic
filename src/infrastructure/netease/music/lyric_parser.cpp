#include "lyric_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace cirrus::netease
{
namespace
{
constexpr qint64 MaxTime = 7LL * 24 * 60 * 60 * 1000;
constexpr int MaxWords = 30000;
const QRegularExpression LineStamp(QStringLiteral(R"(\[(\d{1,6}):([0-5]\d)(?:[.:](\d{1,3}))?\])"));
const QRegularExpression WordLine(QStringLiteral(R"(^\[(\d{1,12}),(\d{1,12})\])"));
const QRegularExpression WordStamp(QStringLiteral(R"(\((\d{1,12}),(\d{1,12}),([+-]?\d{1,10})\))"));
const QRegularExpression Offset(QStringLiteral(R"(^\[offset:([+-]?\d{1,9})\]$)"),
                                QRegularExpression::CaseInsensitiveOption);
const QRegularExpression Metadata(QStringLiteral(R"(^\[(ar|ti|al|by|re|ve|length):.*\]$)"),
                                  QRegularExpression::CaseInsensitiveOption);

std::optional<domain::LyricLine> wordLine(const QString& source,
                                          const QRegularExpressionMatch& header, int& wordCount)
{
    const auto start = header.captured(1).toLongLong();
    const auto duration = header.captured(2).toLongLong();
    if (start > MaxTime || duration > MaxTime - start)
        return std::nullopt;
    domain::LyricLine line{start, start + duration, {}, {}};
    const auto body = source.mid(header.capturedEnd());
    if (body.isEmpty())
        return line;
    auto matches = WordStamp.globalMatch(body);
    QList<QRegularExpressionMatch> stamps;
    while (matches.hasNext()) {
        stamps.append(matches.next());
        if (++wordCount > MaxWords)
            return std::nullopt;
    }
    if (stamps.isEmpty() || stamps.first().capturedStart() != 0)
        return std::nullopt;
    qint64 previous = start;
    for (qsizetype i = 0; i < stamps.size(); ++i) {
        const auto& match = stamps.at(i);
        const auto position = match.captured(1).toLongLong();
        const auto length = match.captured(2).toLongLong();
        if (position < previous || position > *line.endMs || length > *line.endMs - position)
            return std::nullopt;
        const auto end = i + 1 < stamps.size() ? stamps.at(i + 1).capturedStart() : body.size();
        const auto text = body.mid(match.capturedEnd(), end - match.capturedEnd());
        line.words.append({position, length, text});
        line.text += text;
        previous = position; // overlapping durations are permitted; no artificial stretching
    }
    return line;
}
} // namespace

domain::LyricTrack parseLyricText(const QString& source)
{
    using namespace domain;
    LyricTrack track;
    if (source.trimmed().isEmpty()) {
        track.availability = LyricAvailability::Empty;
        return track;
    }
    track.availability = LyricAvailability::Malformed;
    if (source.size() > 512 * 1024 || !source.isValidUtf16() || source.contains(QChar::Null))
        return track;
    auto rows = source.split(u'\n');
    if (rows.size() > 10000)
        return track;
    int wordCount = 0;
    bool sawTimed = false;
    for (auto row : rows) {
        if (row.endsWith(u'\r'))
            row.chop(1);
        if (row.startsWith(QChar(0xfeff)))
            row.remove(0, 1);
        if (row.isEmpty())
            continue;
        if (row.size() > 16384) {
            ++track.malformedLines;
            continue;
        }
        if (row.startsWith(u'{')) {
            const auto object = QJsonDocument::fromJson(row.toUtf8()).object();
            const auto credits = object.value(QStringLiteral("c"));
            if (!credits.isArray() || credits.toArray().size() > 128) {
                ++track.malformedLines;
                continue;
            }
            QString text;
            for (const auto part : credits.toArray())
                text += part.toObject().value(QStringLiteral("tx")).toString();
            if (!text.isEmpty())
                track.credits.append(text);
            continue; // ignore embedded navigation/image URLs and credit timestamps
        }
        const auto offset = Offset.match(row);
        if (offset.hasMatch()) {
            const auto value = offset.captured(1).toLongLong();
            if (value < -MaxTime || value > MaxTime)
                ++track.malformedLines;
            else
                track.providerOffsetMs =
                    -value; // positive LRC offset advances; domain positive delays
            continue;
        }
        if (Metadata.match(row).hasMatch())
            continue;
        const auto header = WordLine.match(row);
        if (header.hasMatch()) {
            const auto line = wordLine(row, header, wordCount);
            if (wordCount > MaxWords)
                return LyricTrack{LyricAvailability::Malformed};
            if (!line) {
                ++track.malformedLines;
                continue;
            }
            track.lines.append(*line);
            track.timing = LyricTiming::Words;
            sawTimed = true;
            continue;
        }
        QList<qint64> starts;
        qsizetype offsetInRow = 0;
        bool invalid = false;
        while (true) {
            const auto stamp = LineStamp.match(row, offsetInRow, QRegularExpression::NormalMatch,
                                               QRegularExpression::AnchorAtOffsetMatchOption);
            if (!stamp.hasMatch())
                break;
            auto fraction = stamp.captured(3);
            if (!fraction.isEmpty())
                fraction = fraction.leftJustified(3, u'0');
            const auto ms = stamp.captured(1).toLongLong() * 60000 +
                            stamp.captured(2).toLongLong() * 1000 + fraction.toLongLong();
            if (ms > MaxTime || starts.size() >= 128) {
                invalid = true;
                break;
            }
            starts.append(ms);
            offsetInRow = stamp.capturedEnd();
        }
        if (invalid || (starts.isEmpty() && row.startsWith(u'['))) {
            ++track.malformedLines;
            continue;
        }
        if (starts.isEmpty())
            track.lines.append({{}, {}, row, {}});
        else {
            for (auto start : starts)
                track.lines.append({start, {}, row.mid(offsetInRow), {}});
            if (track.timing != LyricTiming::Words)
                track.timing = LyricTiming::Lines;
            sawTimed = true;
        }
        if (track.lines.size() > 10000)
            return LyricTrack{LyricAvailability::Malformed};
    }
    if (sawTimed) {
        std::stable_sort(track.lines.begin(), track.lines.end(),
                         [](const LyricLine& a, const LyricLine& b) {
                             return a.startMs && (!b.startMs || *a.startMs < *b.startMs);
                         });
        // Infer LRC end from the next DISTINCT time, retain same-time duet lines.
        std::optional<qint64> next, current;
        for (auto it = track.lines.rbegin(); it != track.lines.rend(); ++it) {
            if (!it->startMs)
                continue;
            if (current != it->startMs) {
                next = current;
                current = it->startMs;
            }
            if (!it->endMs)
                it->endMs = next;
        }
    }
    if (!track.lines.isEmpty())
        track.availability = LyricAvailability::Present;
    else if (track.malformedLines == 0)
        track.availability = LyricAvailability::Empty;
    return track;
}
} // namespace cirrus::netease
