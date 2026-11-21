#pragma once
#include "domain/music/music_types.h"
#include "domain/playback/playback_types.h"
#include <QList>

namespace cirrus::application
{
// Where a song is sung, from its lyrics (docs/tasks/automix.md §4.2), in time order: by
// the words' timing when there is one, else from line starts (a line lasts until the next
// one, a blank break included, 8 s at most). `offsetMs`: the user's lyric offset, on top
// of the provider's. Empty for instrumental songs and untimed lyrics; credit lines
// ("作词：…") aren't singing.
[[nodiscard]] QList<domain::VocalSpan> vocalSpans(const domain::SongLyrics& lyrics,
                                                  qint64 offsetMs = 0);
} // namespace cirrus::application
