#!/usr/bin/env bash
# The Ubuntu 22.04 packages that CI and the Linux packages build with (Qt itself comes
# from the Qt installer, FFmpeg and QtKeychain from packaging/deps/). 22.04 keeps the
# packages' glibc requirement low (2.35); Qt 6.11's own binaries need 2.34. GTK 3 and
# Xrandr are what Qt's GTK theme and FFmpeg plugins link: the GitHub runner has them,
# a plain 22.04 doesn't, and dpkg-shlibdeps needs them to name the deb's dependencies.
set -euo pipefail
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    g++-12 ccache ninja-build nasm patchelf pkg-config file dpkg-dev \
    libssl-dev zlib1g-dev libzstd-dev libsecret-1-dev \
    libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libwayland-dev \
    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
    libfontconfig1 libdbus-1-3 dbus libpulse0 \
    libgtk-3-0 libxrandr2 \
    desktop-file-utils appstream
# GCC 12 is the newest compiler whose libstdc++ stock 22.04 already has.
if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "CC=gcc-12" >> "$GITHUB_ENV"
    echo "CXX=g++-12" >> "$GITHUB_ENV"
fi
