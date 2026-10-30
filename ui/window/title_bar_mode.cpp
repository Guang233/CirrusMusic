#include "title_bar_mode.h"

#include <QCommandLineParser>

namespace cirrus::ui::titlebar
{

namespace
{
// XDG_CURRENT_DESKTOP names of tiling window managers.
constexpr QLatin1StringView kTilingDesktops[] = {
    QLatin1StringView("niri"),  QLatin1StringView("sway"), QLatin1StringView("hyprland"),
    QLatin1StringView("river"), QLatin1StringView("i3"),   QLatin1StringView("dwl"),
};

std::optional<Mode> named(const QString& value, const char* source)
{
    if (value.isEmpty())
        return std::nullopt;
    const auto mode = fromName(value);
    if (!mode)
        qWarning("ignoring %s=%s: expected custom, system or hidden", source, qPrintable(value));
    return mode;
}
} // namespace

Mode platformDefault()
{
    // Colon-separated, e.g. "niri" or "sway:wlroots".
    const QString desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    for (const auto desktop : QStringView(desktops).split(QLatin1Char(':'))) {
        for (const auto tiling : kTilingDesktops) {
            if (desktop.compare(tiling, Qt::CaseInsensitive) == 0)
                return Hidden;
        }
    }
    return Custom;
}

std::optional<Mode> fromName(QStringView name)
{
    if (name == QLatin1StringView("custom"))
        return Custom;
    if (name == QLatin1StringView("system"))
        return System;
    if (name == QLatin1StringView("hidden"))
        return Hidden;
    return std::nullopt;
}

QCommandLineOption option()
{
    return QCommandLineOption(
        QStringLiteral("titlebar"),
        QStringLiteral("Title bar: custom, system or hidden (default: by desktop)."),
        QStringLiteral("mode"));
}

Mode resolve(const QCommandLineParser& parser)
{
    if (const auto mode = named(parser.value(option()), "--titlebar"))
        return *mode;
    if (const auto mode = named(qEnvironmentVariable("CIRRUS_TITLEBAR"), "CIRRUS_TITLEBAR"))
        return *mode;
    return platformDefault();
}

} // namespace cirrus::ui::titlebar
