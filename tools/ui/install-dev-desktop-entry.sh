#!/usr/bin/env bash
# Gives the development build its dock/taskbar icon on Linux. Wayland compositors find
# a window's icon through its app ID (com.guang233.Cirrus) and the desktop file of that
# name, not through the icon the app sets; a dev build has no desktop file.
#
#   tools/ui/install-dev-desktop-entry.sh [preset]   # default linux-dev-local
#   tools/ui/install-dev-desktop-entry.sh --remove
#
# Writes into ~/.local/share (applications/, icons/hicolor/). Remove it before
# installing a Cirrus package: the user's desktop file hides the package's.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
id=com.guang233.Cirrus
desktop="$data/applications/$id.desktop"
sizes=(16 24 32 48 64 128 256 512)

refresh() {
    command -v update-desktop-database >/dev/null && update-desktop-database -q "$data/applications" || true
    command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$data/icons/hicolor" || true
}

if [[ "${1:-}" == --remove ]]; then
    rm -f "$desktop" "$data/icons/hicolor/scalable/apps/$id.svg"
    for size in "${sizes[@]}"; do rm -f "$data/icons/hicolor/${size}x${size}/apps/$id.png"; done
    refresh
    echo "removed $desktop and the icons"
    exit 0
fi

exe="$root/build/${1:-linux-dev-local}/appCirrusMusic"
[[ -x "$exe" ]] || echo "note: $exe does not exist yet; build it first" >&2
mkdir -p "$data/applications" "$data/icons/hicolor/scalable/apps"
for size in "${sizes[@]}"; do
    install -Dm644 "$root/ui/app/icons/cirrus-$size.png" "$data/icons/hicolor/${size}x${size}/apps/$id.png"
done
install -m644 "$root/ui/app/icons/cirrus.svg" "$data/icons/hicolor/scalable/apps/$id.svg"
sed -e "s|@CIRRUS_DESKTOP_EXEC@|\"$exe\"|" -e 's|^Name=Cirrus$|Name=Cirrus (dev)|' \
    "$root/packaging/linux/$id.desktop.in" > "$desktop"
refresh
echo "wrote $desktop (Exec: $exe)"
