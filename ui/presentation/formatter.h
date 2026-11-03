#pragma once

#include "types.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Display formatting for QML (App.format) and ViewModels; pages never format
// durations or counts themselves (docs/ui/presentation.md §4.4).
class Formatter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.format")

  public:
    using QObject::QObject;

    // "3:41", "1:02:05"; "--:--" when unknown (negative).
    Q_INVOKABLE static QString duration(qint64 milliseconds);
    // Compact count in the system language: "9,876", "1.2万" / "12K".
    Q_INVOKABLE static QString count(qint64 value);
    // A day: "10月28日" this year, "2024年10月28日" before; empty for 0 (unknown).
    Q_INVOKABLE static QString date(qint64 msSinceEpoch);
    // When something was posted: "刚刚", "5 分钟前", "14:03" today, "昨天 14:03", then
    // like date(); empty for 0.
    Q_INVOKABLE static QString relativeTime(qint64 msSinceEpoch);
    // Always with the year: "2024年5月1日"; empty for 0.
    Q_INVOKABLE static QString fullDate(qint64 msSinceEpoch);
    // A list's total length: "48 分钟", "1 小时 52 分钟" (minutes rounded up).
    Q_INVOKABLE static QString totalLength(qint64 milliseconds);
    // Artist names joined for one line: "A / B".
    [[nodiscard]] static QString artists(const QList<ArtistRef>& artists);
};

} // namespace cirrus::ui
