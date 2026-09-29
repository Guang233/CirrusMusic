#pragma once

#include "domain/account/session_types.h"

#include <QJsonObject>
#include <optional>

namespace cirrus::netease
{
// Official root account/profile only; no wrapper data, fractional/unsafe numeric
// IDs, partial/anonymous profile or conflicting account identity. Wide IDs require
// canonical decimal strings; numeric IDs are limited to positive safe integers.
[[nodiscard]] std::optional<domain::AccountIdentity> mapAccountIdentity(const QJsonObject& object);
} // namespace cirrus::netease
