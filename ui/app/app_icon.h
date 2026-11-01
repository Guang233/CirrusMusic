#pragma once

#include <QIcon>

namespace cirrus::ui
{

// Placeholder app icon (window and tray) until the app identity is decided (UQ-06).
[[nodiscard]] inline QIcon appIcon()
{
    return QIcon(QStringLiteral(":/qt/qml/CirrusMusic/icons/cirrus-music.svg"));
}

} // namespace cirrus::ui
