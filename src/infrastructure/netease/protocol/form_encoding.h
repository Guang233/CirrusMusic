#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringView>

namespace cirrus::netease
{

struct FormField {
    QString name;
    QString value;
};

// WHATWG application/x-www-form-urlencoded, matching URLSearchParams.
// Inputs are already typed/serialized strings; no implicit JS value coercion.
[[nodiscard]] QByteArray encodeFormComponent(QStringView value);

// Preserve field order and repeated names. This is not QUrlQuery encoding.
[[nodiscard]] QByteArray encodeForm(const QList<FormField>& fields);

} // namespace cirrus::netease
