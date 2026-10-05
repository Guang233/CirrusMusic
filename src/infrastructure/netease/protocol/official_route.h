#pragma once

#include <QStringView>
#include <QUrl>

#include <optional>

namespace cirrus::netease
{

enum class Protocol { Api, Weapi, Eapi, Xeapi };
enum class OfficialDestination { Default, ListeningLog };

// Internal route construction only, NOT an operation registry or a signer.
// Accept a canonical /api/... path; reject full URLs, queries and escapes.
// Hosts are pinned defaults. Special service profiles require separate review.
[[nodiscard]] std::optional<QUrl>
officialRequestUrl(Protocol protocol, QStringView logicalPath,
                   OfficialDestination destination = OfficialDestination::Default);

} // namespace cirrus::netease
