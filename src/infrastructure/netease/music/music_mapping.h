#pragma once
#include "domain/music/catalog_types.h"
#include <QJsonObject>

// Infrastructure-only mapping primitives shared by song and catalog schemas.
namespace cirrus::netease::mapping
{
QString providerId(const QJsonValue& value);
std::optional<domain::Album> albumFrom(const QJsonObject& object);
QUrl imageFrom(const QJsonValue& value);
std::optional<qint64> nonnegative(const QJsonValue& value);
bool textField(const QJsonValue& value);
bool unknownId(const QJsonValue& value);
std::optional<domain::Song> songFrom(const QJsonObject& object);
} // namespace cirrus::netease::mapping
