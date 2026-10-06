#pragma once
#include "domain/music/entity_search_types.h"
#include <QJsonObject>
namespace cirrus::netease::mapping
{
std::optional<domain::Podcast> podcastFrom(const QJsonObject& object);
std::optional<domain::Voice> voiceFrom(const QJsonObject& object);
std::optional<domain::DiscoveryPlaylist> discoveryPlaylistFrom(const QJsonObject& object);
} // namespace cirrus::netease::mapping
