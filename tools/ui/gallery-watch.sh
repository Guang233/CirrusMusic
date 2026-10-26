#!/usr/bin/env bash
# Live preview: build and start the gallery, then rebuild whenever UI sources change
# and restart it if the build succeeds (QML is compiled into the binary, so a
# rebuild is needed). The gallery reopens the last page, cover and mode.
#
#   tools/ui/gallery-watch.sh [gallery args...]     e.g. --page buttons
#   PRESET=linux-release-local tools/ui/gallery-watch.sh
#
# Needs inotifywait (inotify-tools). Ctrl+C stops both the watcher and the gallery.
set -u
cd "$(dirname "$0")/../.."

preset="${PRESET:-linux-dev-local}"
gallery="build/$preset/appCirrusGallery"
watched=(ui third_party/m3shapes third_party/material-color-utilities)
pattern='(\.(qml|js|cpp|h|cc|hpp|frag|vert|json|ttf|jpg|png)|CMakeLists\.txt)$'

if ! command -v inotifywait >/dev/null; then
    echo "gallery-watch: 需要 inotifywait（安装 inotify-tools）" >&2
    exit 1
fi

pid=""
watcher=""
start() {
    "$gallery" "$@" &
    pid=$!
}
stop() {
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid"
        wait "$pid" 2>/dev/null
    fi
    pid=""
}
build() {
    cmake --build --preset "$preset" --target appCirrusGallery
}
# inotifywait runs in the background and the loop waits on it, so signals (Ctrl+C,
# VS Code stopping the task) are handled at once instead of after the next event.
cleanup() {
    [ -n "$watcher" ] && kill "$watcher" 2>/dev/null
    stop
    exit 0
}
trap cleanup INT TERM HUP

[ -f "build/$preset/build.ninja" ] || cmake --preset "$preset" || exit 1
build && start "$@"

while true; do
    inotifywait -qq -r -e close_write,create,delete,move --include "$pattern" "${watched[@]}" &
    watcher=$!
    wait "$watcher" || exit 1
    watcher=""
    sleep 0.3 # let editors finish multi-file saves
    printf '\n== gallery-watch: 源文件有改动，重新构建（%s）\n' "$(date +%T)"
    if build; then
        stop
        start "$@"
    else
        echo "== gallery-watch: 构建失败，保留当前窗口；修好后保存即可重试"
    fi
done
