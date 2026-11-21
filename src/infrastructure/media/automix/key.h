#pragma once
#include "logging/log.h"
#include "rhythm.h"
#include <QString>
#include <QtGlobal>

namespace cirrus::media::automix
{

// A musical key (docs/tasks/automix.md §4.2): its tonic as a pitch class (C = 0) and mode.
struct Key {
    int tonic = -1; // -1: none found (too little of it with pitch: drums, silence)
    bool minor = false;
    // How much better it fits than any key two or more fifths away (0: no better). Its
    // relative and its neighbours on the circle of fifths often fit about as well: they go
    // with the same keys.
    float confidence = 0;
    [[nodiscard]] bool valid() const { return tonic >= 0; }
};

// The key of [fromMs, toMs) of a file's pitch classes: the one whose Krumhansl–Kessler
// profile (Krumhansl 1990) they correlate best with.
[[nodiscard]] Key findKey(const RhythmFeatures& features, double fromMs, double toMs);

// "C", "F#m" and the like, with its confidence: "F#m 0.12" ("" for none).
[[nodiscard]] QString keyName(const Key& key);

// Steps between two keys on the circle of fifths (0–6), a minor key at its relative
// major: 0 the same key or its relative, 1 a fifth up or down.
[[nodiscard]] int fifthsApart(const Key& a, const Key& b);

// Whether two keys' melodies can sound together for long.
enum class Harmony {
    Unknown, // either key is not sure
    Close,   // the same, the relative, a fifth apart: they go together
    Clash,   // further apart: overlap them briefly
};
CIRRUS_LOG_ENUM(Harmony, Unknown, Close, Clash)
[[nodiscard]] Harmony harmony(const Key& a, const Key& b);

} // namespace cirrus::media::automix
