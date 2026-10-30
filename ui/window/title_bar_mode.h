#pragma once

#include <QCommandLineOption>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <optional>

class QCommandLineParser;

// How the window is framed (docs/ui/README.md §6.1): `AppWindow { titleBar: TitleBarMode.Hidden }`.
namespace cirrus::ui::titlebar
{
Q_NAMESPACE
QML_NAMED_ELEMENT(TitleBarMode)

// Custom: our own title bar, frame and window buttons.
// System: native decorations.
// Hidden: no title bar controls, frame or shadow; for tiling window managers,
// which manage windows from the keyboard and draw their own borders.
enum Mode { Custom, System, Hidden };
Q_ENUM_NS(Mode)

// Hidden on known tiling window managers, Custom elsewhere.
[[nodiscard]] Mode platformDefault();
// "custom", "system" or "hidden".
[[nodiscard]] std::optional<Mode> fromName(QStringView name);
// --titlebar=<mode>; an escape hatch when a mode misbehaves on some desktop.
[[nodiscard]] QCommandLineOption option();
// The --titlebar option, else CIRRUS_TITLEBAR, else platformDefault().
[[nodiscard]] Mode resolve(const QCommandLineParser& parser);
} // namespace cirrus::ui::titlebar
