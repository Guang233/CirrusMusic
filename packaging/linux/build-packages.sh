#!/usr/bin/env bash
# Builds the Linux packages (packaging/README.md): a deb that installs into
# /opt/cirrus with its own Qt, and an AppImage. Both carry the same deployed tree.
#
#   packaging/linux/build-packages.sh <qt prefix> <deps prefix> <out dir> [deb|appimage|stage]...
#
# <deps prefix> holds FFmpeg and QtKeychain from packaging/deps/. With no package kinds
# given it builds both. "stage" only builds and installs (for a local check of the tree).
# The deb needs dpkg-deb and dpkg-shlibdeps (Debian/Ubuntu); the AppImage downloads
# linuxdeploy into the build directory.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
qt=${1:?usage: build-packages.sh <qt prefix> <deps prefix> <out dir> [deb|appimage|stage]...}
deps=$(cd "${2:?missing deps prefix}" && pwd)
mkdir -p "${3:?missing out dir}"
out=$(cd "$3" && pwd)
shift 3
kinds=("$@")
[[ ${#kinds[@]} -gt 0 ]] || kinds=(deb appimage)

version=$(sed -nE 's/^project\(CirrusMusic VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
build=${CIRRUS_PACKAGE_BUILD_DIR:-$root/build/package-linux}
arch=x86_64

PKG_CONFIG_PATH="$deps/lib/pkgconfig" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt;$deps" \
    -DBUILD_TESTING=OFF \
    -DCIRRUS_BUILD_GALLERY=OFF \
    -DCIRRUS_EXECUTABLE=cirrus \
    -DCIRRUS_DEPLOY_QT=ON
cmake --build "$build" --parallel "${JOBS:-4}"

# install <dir> <prefix>: the app with Qt and the bundled libraries under <dir><prefix>.
install_tree() {
    rm -rf "$1"
    DESTDIR="$1" cmake --install "$build" --prefix "$2"
    # Debug symbols are not shipped. Only files that still have them: stripping Qt's
    # already stripped libraries again (binutils 2.38) breaks the alignment of its
    # FFmpeg's libavutil.so.59, which then no longer loads.
    find "$1" -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c \
        'file -b "$1" | grep -q "^ELF.*not stripped" && strip --strip-unneeded "$1"' _ {} \;
}

make_deb() {
    local stage="$build/deb" pkg=cirrus
    install_tree "$stage" /opt/cirrus
    # Desktop integration goes where the desktop looks for it; the rest stays in /opt.
    mkdir -p "$stage/usr/bin" "$stage/usr/share/doc/$pkg"
    mv "$stage/opt/cirrus/share/applications" "$stage/opt/cirrus/share/metainfo" \
       "$stage/opt/cirrus/share/icons" "$stage/usr/share/"
    ln -s /opt/cirrus/bin/cirrus "$stage/usr/bin/cirrus"
    # Cirrus's own license, then the third-party software it ships.
    { cat "$root/LICENSE"; echo; cat "$root/packaging/THIRD-PARTY-NOTICES.md"; } \
        >"$stage/usr/share/doc/$pkg/copyright"

    # Dependencies on the system's libraries (everything not in the bundle).
    local tmp; tmp=$(mktemp -d)
    mkdir -p "$tmp/debian"
    printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$pkg" "$pkg" > "$tmp/debian/control"
    local elves=()
    while IFS= read -r -d '' f; do
        file -b "$f" | grep -q ELF && elves+=("$f")
    done < <(find "$stage/opt/cirrus" -type f -print0)
    local libdirs=() d
    while IFS= read -r d; do libdirs+=("-l$d"); done < <(
        find "$stage/opt/cirrus" -name '*.so*' -printf '%h\n' | sort -u)
    local depends
    depends=$(cd "$tmp" && dpkg-shlibdeps -O --ignore-missing-info "${libdirs[@]}" \
        -e"${elves[@]}" 2> >(grep -v 'warning:' >&2) | sed -n 's/^shlibs:Depends=//p')
    # Qt's TLS takes the system's root certificates (not a library, so not found above),
    # and QtKeychain opens libsecret at run time, to reach the keyring that keeps the
    # sign-in (KWallet it reaches over D-Bus).
    depends+=", ca-certificates, libsecret-1-0"
    rm -rf "$tmp"

    mkdir -p "$stage/DEBIAN"
    cat > "$stage/DEBIAN/control" <<EOF
Package: $pkg
Version: $version
Architecture: amd64
Maintainer: guang233
Section: sound
Priority: optional
Installed-Size: $(du -sk --exclude=DEBIAN "$stage" | cut -f1)
Depends: $depends
Recommends: gnome-keyring | kwalletmanager | keepassxc
Description: Third-party NetEase Cloud Music client
 Cirrus plays music from NetEase Cloud Music on the desktop. It carries its own
 Qt in /opt/cirrus. It is not affiliated with NetEase.
EOF
    dpkg-deb --build --root-owner-group -Zxz "$stage" "$out/cirrus_${version}_amd64.deb"
}

make_appimage() {
    local appdir="$build/AppDir" tools="$build/appimage-tools"
    install_tree "$appdir" /usr
    mkdir -p "$tools"
    if [[ ! -x "$tools/linuxdeploy" ]]; then
        curl -fsSL --retry 3 -o "$tools/linuxdeploy" \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-$arch.AppImage"
        chmod +x "$tools/linuxdeploy"
    fi
    # Qt is already deployed (CIRRUS_DEPLOY_QT); linuxdeploy adds the remaining
    # non-base libraries of the executable, Qt's plugins and QML plugins, then packs.
    # QtKeychain opens libsecret at run time, so linuxdeploy does not see it. It must be
    # this system's, next to the glib that gets bundled: the host's libsecret wants the
    # host's newer glib and fails to load ("undefined symbol: g_task_set_static_name"),
    # and then the sign-in is not kept (0.1.0).
    local libsecret
    libsecret=$(ldconfig -p | awk '$1 == "libsecret-1.so.0" { print $NF; exit }')
    [[ -n "$libsecret" ]] || { echo "libsecret-1.so.0 not found (libsecret-1-0)" >&2; exit 1; }
    local deps_only=()
    while IFS= read -r d; do deps_only+=(--deploy-deps-only "$d"); done < <(
        find "$appdir/usr/plugins" "$appdir/usr/qml" -name '*.so' -printf '%h\n' | sort -u)
    (cd "$build" && APPIMAGE_EXTRACT_AND_RUN=1 LDAI_OUTPUT="$out/Cirrus-$version-$arch.AppImage" \
        ARCH=$arch \
        "$tools/linuxdeploy" --appdir "$appdir" \
            --executable "$appdir/usr/bin/cirrus" \
            --desktop-file "$appdir/usr/share/applications/com.guang233.Cirrus.desktop" \
            --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/com.guang233.Cirrus.png" \
            --library "$libsecret" \
            "${deps_only[@]}" \
            --output appimage)
    [[ -e "$appdir/usr/lib/libsecret-1.so.0" ]] || { echo "libsecret was not bundled" >&2; exit 1; }
}

for kind in "${kinds[@]}"; do
    case $kind in
        deb) make_deb ;;
        appimage) make_appimage ;;
        stage) install_tree "$build/stage" /opt/cirrus ;;
        *) echo "unknown package kind: $kind" >&2; exit 2 ;;
    esac
done
ls -l "$out"
