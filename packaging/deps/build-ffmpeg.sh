#!/usr/bin/env bash
# Builds the FFmpeg that Linux packages bundle (packaging/README.md): shared, LGPL
# (no --enable-gpl, no external libraries) and only what the audio decoder opens, the
# MP3 and FLAC demuxers and decoders (src/infrastructure/media/audio_decoder.cpp).
# Its sonames (libavcodec.so.63 …) differ from the FFmpeg 7.1 that Qt Multimedia ships
# (libavcodec.so.61), so both live in the bundle's lib/.
#
#   packaging/deps/build-ffmpeg.sh <prefix>
#
# Needs a C compiler, make, nasm and patchelf. Versions and checksums: versions.sh.
set -euo pipefail

source "$(dirname "$0")/versions.sh"
VERSION=$FFMPEG_VERSION

prefix=${1:?usage: build-ffmpeg.sh <prefix>}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fetch "$FFMPEG_URL" "$FFMPEG_SHA256" "$work/ffmpeg.tar.xz"
tar -xf "$work/ffmpeg.tar.xz" -C "$work"
cd "$work/ffmpeg-$VERSION"

./configure \
    --prefix="$prefix" \
    --libdir="$prefix/lib" \
    --enable-shared --disable-static --enable-pic \
    --disable-programs --disable-doc --disable-debug \
    --disable-autodetect --disable-network \
    --disable-avdevice --disable-avfilter --disable-swscale \
    --disable-everything \
    --enable-demuxer=mp3,flac \
    --enable-decoder=mp3,mp3float,flac \
    --enable-parser=mpegaudio,flac
make -j"${JOBS:-$(nproc)}"
make install

# The libraries find each other in their own directory (lib/ of the bundle).
for lib in "$prefix"/lib/lib{avformat,avcodec,avutil,swresample}.so.*.*.*; do
    patchelf --set-rpath '$ORIGIN' "$lib"
done
mkdir -p "$prefix/share/licenses/ffmpeg"
cp COPYING.LGPLv2.1 LICENSE.md "$prefix/share/licenses/ffmpeg/"
echo "FFmpeg $VERSION installed in $prefix"
