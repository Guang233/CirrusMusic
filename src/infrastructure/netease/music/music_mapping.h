#pragma once
#include "domain/music/catalog_types.h"
#include "logging/log.h"
#include <QJsonObject>

// Infrastructure-only mapping primitives shared by song and catalog schemas.
namespace cirrus::netease::mapping
{
// Which check a code-200 response failed (debug; the gateway logs the failure itself as
// music.invalid). `row` is the list row, -1 for the response.
inline void rejectedBecause(const char* reason, qsizetype row = -1)
{
    qCDebug(lcApi) << log::event("music.invalid_why") << log::field("reason", reason)
                   << log::field("row", row);
}
QString providerId(const QJsonValue& value);
std::optional<domain::Album> albumFrom(const QJsonObject& object);
QUrl imageFrom(const QJsonValue& value);
std::optional<qint64> nonnegative(const QJsonValue& value);
bool textField(const QJsonValue& value);
bool unknownId(const QJsonValue& value);
std::optional<domain::Song> songFrom(const QJsonObject& object);
} // namespace cirrus::netease::mapping
