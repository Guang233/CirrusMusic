#!/usr/bin/env bash
# Builds the macOS package (packaging/README.md): Cirrus.app with its own Qt and libraries,
# ad-hoc signed, in a dmg next to a link to /Applications.
#
#   packaging/macos/build-package.sh <qt prefix> <deps prefix> <out dir>
#
# <deps prefix> holds FFmpeg and QtKeychain from packaging/deps/; OpenSSL and zstd come
# from Homebrew (openssl@3, zstd), so the package needs the macOS version Homebrew built them
# for (MACOSX_DEPLOYMENT_TARGET, 15.0 on the release runner). Apple Silicon only.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
qt=${1:?usage: build-package.sh <qt prefix> <deps prefix> <out dir>}
deps=$(cd "${2:?missing deps prefix}" && pwd)
mkdir -p "${3:?missing out dir}"
out=$(cd "$3" && pwd)

version=$(sed -nE 's/^project\(CirrusMusic VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
build=${CIRRUS_PACKAGE_BUILD_DIR:-$root/build/package-macos}
stage=$build/stage
app=$stage/Cirrus.app
brew=$(brew --prefix)

PKG_CONFIG_PATH="$deps/lib/pkgconfig" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt;$deps;$brew/opt/openssl@3;$brew/opt/zstd" \
    -DOPENSSL_ROOT_DIR="$brew/opt/openssl@3" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}" \
    -DBUILD_TESTING=OFF \
    -DCIRRUS_BUILD_GALLERY=OFF \
    -DCIRRUS_EXECUTABLE=Cirrus \
    -DCIRRUS_PACKAGE=macos \
    -DCIRRUS_DEPLOY_QT=ON
cmake --build "$build" --parallel "${JOBS:-4}"

rm -rf "$stage"
cmake --install "$build" --prefix "$stage"

# Only SQLite: the other drivers need client libraries nobody has (macdeployqt deploys every
# driver Qt ships and ignores the deploy script's plugin lists).
find "$app/Contents/PlugIns/sqldrivers" -type f ! -name 'libqsqlite.dylib' -delete

# Every library must come from the bundle or the system. A path into the build machine's
# Qt, deps or Homebrew would make the app fail to start anywhere else.
bad=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    while IFS= read -r dep; do
        case "$dep" in
            /System/* | /usr/lib/* | @executable_path/* | @loader_path/* | @rpath/*) ;;
            *) echo "::error::$(basename "$f") links $dep"; bad=1 ;;
        esac
    done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
done < <(find "$app/Contents" -type f -print0)
(( bad == 0 ))
for lib in libavcodec.63 libavformat.63 libqt6keychain libcrypto.3 libzstd.1; do
    ls "$app/Contents/Frameworks/$lib"* > /dev/null 2>&1 ||
        { echo "::error::$lib is not in the bundle"; exit 1; }
done

# Ad-hoc signature: Apple Silicon runs only signed code, and macdeployqt's install_name_tool
# changes invalidated the signatures the linker made. Not notarized (no developer ID):
# Gatekeeper asks the user once (README).
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"

dmgroot=$build/dmg
rm -rf "$dmgroot"
mkdir -p "$dmgroot"
cp -R "$app" "$dmgroot/"
ln -s /Applications "$dmgroot/Applications"
dmg="$out/Cirrus-$version-macos-arm64.dmg"
rm -f "$dmg"
hdiutil create -volname "Cirrus $version" -srcfolder "$dmgroot" -fs HFS+ -format UDZO "$dmg"
echo "Built $dmg"
