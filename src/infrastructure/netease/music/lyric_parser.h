#pragma once

#include "domain/music/music_types.h"

namespace cirrus::netease
{
// LRC/YRC content parser, independent of HTTP/JSON envelopes. Missing fields are
// represented by the mapper; empty input is Empty, not Instrumental.
[[nodiscard]] domain::LyricTrack parseLyricText(const QString& text);
} // namespace cirrus::netease
