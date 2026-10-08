#pragma once
#include "application/ports/music_gateway.h"
#include <QJsonObject>
namespace cirrus::netease
{
[[nodiscard]] application::MusicResult
mapSongWikiTextResponse(const domain::SongWikiTextQuery& query, const QJsonObject& object);
}
