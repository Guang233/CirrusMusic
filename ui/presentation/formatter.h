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
    // Artist names joined for one line: "A / B".
    [[nodiscard]] static QString artists(const QList<ArtistRef>& artists);
};

} // namespace cirrus::ui
