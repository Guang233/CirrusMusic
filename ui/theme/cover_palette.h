#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Colors extracted from one cover image.
//  seed:     best theme source color (MD3 scoring), drives the shell scheme
//  accents:  up to 4 scored theme candidates, best first
//  dominant: up to 5 most common quantized colors, for the player background
class CoverPalette
{
    Q_GADGET
    QML_VALUE_TYPE(coverPalette)
    Q_PROPERTY(bool valid MEMBER valid CONSTANT)
    Q_PROPERTY(QColor seed MEMBER seed CONSTANT)
    Q_PROPERTY(QList<QColor> accents MEMBER accents CONSTANT)
    Q_PROPERTY(QList<QColor> dominant MEMBER dominant CONSTANT)

  public:
    bool valid = false;
    QColor seed;
    QList<QColor> accents;
    QList<QColor> dominant;
};

// Decodes `path` (local file or ":/" resource) at reduced size and quantizes it.
// Blocking and thread-safe; call it off the GUI thread.
[[nodiscard]] CoverPalette extractCoverPalette(const QString& path);

} // namespace cirrus::ui
