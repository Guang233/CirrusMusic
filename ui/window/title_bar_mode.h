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

// Window decorations; the app's top bar is there in every mode.
// Custom: drawn by the app: window buttons, frame and shadow.
// System: native decorations.
// Hidden: none (no buttons, frame or shadow); for tiling window managers, which
// manage windows from the keyboard and draw their own borders.
enum Mode { Custom, System, Hidden };
Q_ENUM_NS(Mode)

// Hidden on known tiling window managers, Custom elsewhere.
[[nodiscard]] Mode platformDefault();
// "custom", "system" or "hidden".
[[nodiscard]] std::optional<Mode> fromName(QStringView name);
// --titlebar=<mode>; an escape hatch when a mode misbehaves on some desktop.
[[nodiscard]] QCommandLineOption option();
// The mode asked for by the --titlebar option, else by CIRRUS_TITLEBAR.
[[nodiscard]] std::optional<Mode> requested(const QCommandLineParser& parser);
// requested(), else platformDefault().
[[nodiscard]] Mode resolve(const QCommandLineParser& parser);
} // namespace cirrus::ui::titlebar
