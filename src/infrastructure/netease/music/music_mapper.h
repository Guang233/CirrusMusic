#pragma once

#include "application/ports/music_gateway.h"
#include <QJsonObject>

namespace cirrus::netease
{
[[nodiscard]] application::MusicResult mapMusicResponse(const application::MusicQuery& query,
                                                        const QJsonObject& object);
} // namespace cirrus::netease
