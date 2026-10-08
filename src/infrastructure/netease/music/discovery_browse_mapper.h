#pragma once
#include "application/ports/music_gateway.h"
#include <QJsonObject>
namespace cirrus::netease
{
[[nodiscard]] application::MusicResult
mapDiscoveryBrowseResponse(const domain::DiscoveryBrowseQuery& query, const QJsonObject& object);
} // namespace cirrus::netease
