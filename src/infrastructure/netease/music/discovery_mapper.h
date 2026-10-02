#pragma once
#include "application/ports/music_gateway.h"
#include <QJsonObject>

namespace cirrus::netease
{
application::MusicResult mapDiscoveryResponse(const application::MusicQuery& query,
                                              const QJsonObject& object);
}
