#pragma once
#include "domain/music/resource_cache_types.h"

namespace cirrus::cache
{
// Strict cache schema, not a provider-response decoder or generic serialization port.
QByteArray encodeCacheValue(const domain::CacheValue& value, domain::CacheKind kind);
std::optional<domain::CacheValue> decodeCacheValue(QByteArray bytes, domain::CacheKind kind);
bool validCoverUrl(const QUrl& url);
bool validCoverSize(int size);
QString coverCacheKey(const QUrl& url, int size);
} // namespace cirrus::cache
