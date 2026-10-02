#pragma once
#include "domain/playback/playback_types.h"

namespace cirrus::domain
{
struct QueueEntry {
    QUuid id;
    SongId song;
};
enum class QueueOrder { Sequential, Shuffle };
enum class QueueRepeat { Off, All, One };
struct QueueSnapshot {
    QList<QueueEntry> entries;
    QUuid active;
    QueueOrder order{};
    QueueRepeat repeat{};
};
struct ActiveLyricWord {
    qsizetype line = 0;
    qsizetype word = 0;
    friend bool operator==(const ActiveLyricWord&, const ActiveLyricWord&) = default;
};
struct LyricCursor {
    QList<qsizetype> lines;
    QList<ActiveLyricWord> words;
    friend bool operator==(const LyricCursor&, const LyricCursor&) = default;
};
struct LyricVersionCursors {
    LyricCursor standard, enhanced;
    friend bool operator==(const LyricVersionCursors&, const LyricVersionCursors&) = default;
};
struct LyricCursors {
    LyricVersionCursors original, translation, romanization;
    friend bool operator==(const LyricCursors&, const LyricCursors&) = default;
};
enum class LyricsState { None, Loading, Ready, Failed };
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::QueueSnapshot)
Q_DECLARE_METATYPE(cirrus::domain::LyricCursors)
Q_DECLARE_METATYPE(cirrus::domain::SongLyrics)
