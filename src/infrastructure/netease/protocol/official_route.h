#pragma once

#include "logging/log.h"

#include <QStringView>
#include <QUrl>

#include <optional>

namespace cirrus::netease
{

enum class Protocol { Api, Weapi, Eapi, Xeapi };
enum class OfficialDestination { Default, TogetherHttp, ClientLog };
CIRRUS_LOG_ENUM(Protocol, Api, Weapi, Eapi, Xeapi)
CIRRUS_LOG_ENUM(OfficialDestination, Default, TogetherHttp, ClientLog)

// Internal route construction only, NOT an operation registry or a signer.
// Accept a canonical /api/... path; reject full URLs, queries and escapes.
// Hosts are pinned defaults. Special service profiles require separate review.
[[nodiscard]] std::optional<QUrl>
officialRequestUrl(Protocol protocol, QStringView logicalPath,
                   OfficialDestination destination = OfficialDestination::Default);

} // namespace cirrus::netease
