#pragma once

#include "types.h"

#include <QList>
#include <QString>

#include <optional>

namespace cirrus::ui
{

// One piece of a lyric line shown with romaji ruby (docs/ui/pages/player.md §6): the
// base text, the romaji over it (empty: none, e.g. punctuation or English) and the
// range [from, to) of the source line it stands for. A kanji run with a reading in
// brackets shows only the kanji, but its range covers the brackets.
struct RubyUnit {
    QString text;
    QString ruby;
    int from = 0;
    int to = 0;
};

// Whether the text has hiragana or katakana: the song is Japanese.
[[nodiscard]] bool containsKana(const QString& text);

// Aligns a line with its romanization by text (§6.3): kana are anchors with known
// readings, kanji runs take what is between them. Null when it doesn't fit exactly.
[[nodiscard]] std::optional<QList<RubyUnit>> alignRuby(const QString& line,
                                                       const QString& romanization);

// Aligns a line's words with syllable-timed romaji by time: each romaji syllable goes
// to the words it is sung over. Null when a word with kana or kanji gets none.
[[nodiscard]] std::optional<QList<RubyUnit>>
alignRubyByTime(const QString& line, const QList<TimedText>& words, const QList<TimedText>& romaji);

} // namespace cirrus::ui
