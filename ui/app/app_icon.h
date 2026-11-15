#pragma once

#include <QIcon>

namespace cirrus::ui
{

// The app icon (window, tray): bitmaps rendered by tools/ui/make_icon.py; the sizes up
// to 32 are a simplified drawing, which one SVG could not give.
[[nodiscard]] inline QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/qt/qml/CirrusMusic/icons/cirrus-%1.png").arg(size),
                     QSize(size, size));
    return icon;
}

} // namespace cirrus::ui
