#pragma once
#include "music_mapping.h"
#include <QJsonArray>
#include <QVariant>

namespace cirrus::netease::mapping
{
inline bool metadataText(const QJsonValue& value, std::optional<QString>& output,
                         qsizetype maximum = 4096)
{
    output.reset();
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isString() || value.toString().size() > maximum ||
        !value.toString().isValidUtf16() || value.toString().contains(QChar::Null))
        return false;
    output = value.toString();
    return true;
}
inline bool metadataList(const QJsonValue& value, std::optional<QStringList>& output)
{
    output.reset();
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isArray() || value.toArray().size() > 100)
        return false;
    QStringList list;
    for (const auto row : value.toArray()) {
        std::optional<QString> text;
        if (!metadataText(row, text) || !text)
            return false;
        list.append(*text);
    }
    output = std::move(list); // explicit [] remains distinct from unknown
    return true;
}
inline bool metadataTimestamp(const QJsonValue& value, std::optional<qint64>& output)
{
    output.reset();
    if (value.isUndefined() || value.isNull())
        return true;
    if (value.toVariant().metaType().id() != QMetaType::LongLong)
        return false;
    const auto n = value.toInteger();
    if (n < -9007199254740991LL || n > 9007199254740991LL)
        return false;
    output = n; // raw provider milliseconds, including zero/pre-1970; no timezone inference
    return true;
}
} // namespace cirrus::netease::mapping
