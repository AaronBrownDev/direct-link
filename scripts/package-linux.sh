#!/bin/bash
#
# Script Name:  package-linux.sh
# Author:       Justin Williams
# Date:         4/20/26
# Description:  A script for packaging the app into an AppImage file
#               that can be run on Linux machines. Cmake will install
#               and package the 
#
# Usage:        ./package-linux.sh [--build]
#
# Options:      --build     Configure, then build install and package
#                           the app. Run when packaging for the first
#                           time or after changing source code.
#               
#
# Notes:        The following must be installed to run this script
#                       cmake
#                       ninja-build
#                       patchelf
#                       wget
#                       clang-18
#                       llvm-dev-18
#                       libclang-dev-18
#                       Qt (via aqtinstall)
#                       LiveKit (at path /opt/livekit-cpp)
#                       TODO: Create documentation for Linux packaging workflow

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
CLIENT_DIR="${REPO_DIR}/client"
QT_VERSION=6.10.2
QT_PLUGINS="/home/justw/Qt/${QT_VERSION}/gcc_64/plugins"

export QMAKE="/home/justw/Qt/${QT_VERSION}/gcc_64/bin/qmake"
export QML_SOURCES_PATHS="${CLIENT_DIR}/src"
export Qt6_DIR="/home/justw/Qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6"
export LD_LIBRARY_PATH="${CLIENT_DIR}/AppDir/usr/lib:/opt/livekit-cpp/build/lib:${CLIENT_DIR}/build/release:${LD_LIBRARY_PATH}"
export LINUXDEPLOY_OUTPUT_APP_NAME="direct-link"
export LINUXDEPLOY_OUTPUT_VERSION="0.1"

BUILD=false
if [ "${1}" == "--build" ]; then
    BUILD=true
fi

# Check for required tools
if ! command -v patchelf &>/dev/null; then
    echo "ERROR: patchelf is not installed. Run: sudo apt-get install patchelf"
    exit 1
fi

if ! command -v wget &>/dev/null; then
    echo "ERROR: wget is not installed. Run: sudo apt-get install wget"
    exit 1
fi

if ! command -v ninja &>/dev/null; then
    echo "ERROR: ninja is not installed. Run: sudo apt-get install ninja-build"
    exit 1
fi

LINUXDEPLOY="${CLIENT_DIR}/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="${CLIENT_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"

if [ ! -f "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy"
    wget -q --show-progress \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
        -O "$LINUXDEPLOY"
    chmod +x "$LINUXDEPLOY"
fi

if [ ! -f "$LINUXDEPLOY_QT" ]; then
    echo "Downloading linuxdeploy-plugin-qt"
    wget -q --show-progress \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
        -O "$LINUXDEPLOY_QT"
    chmod +x "$LINUXDEPLOY_QT"
fi

# Configure and build
if [ "$BUILD" = true ]; then
    echo "Configuring..."
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${CLIENT_DIR}/AppDir/usr" \
        -DLIVEKIT_DIR=/opt/livekit-cpp \
        -DQt6_DIR="${Qt6_DIR}" \
        -B "${CLIENT_DIR}/build/release" \
        -S "${CLIENT_DIR}"

    echo "Building..."
    cmake --build "${CLIENT_DIR}/build/release"
fi

# The following plugins are ignored to avoid dependency errors
# If any additional categories fail during linuxdeploy, add them here
EXCLUDED_PLUGIN_DIRS=(
    "sqldrivers"
)

restore_plugins() {
    for name in "${EXCLUDED_PLUGIN_DIRS[@]}"; do
        if [ -d "${QT_PLUGINS}/${name}.bak" ]; then
            mv "${QT_PLUGINS}/${name}.bak" "${QT_PLUGINS}/${name}"
        fi
    done
}
trap restore_plugins EXIT

for name in "${EXCLUDED_PLUGIN_DIRS[@]}"; do
    if [ -d "${QT_PLUGINS}/${name}" ]; then
        mv "${QT_PLUGINS}/${name}" "${QT_PLUGINS}/${name}.bak"
    fi
done

echo "Installing to AppDir..."
rm -rf "${CLIENT_DIR}/AppDir"
cmake --install "${CLIENT_DIR}/build/release"

cd "$CLIENT_DIR"

# Patches the livekit path so that it does not rely on an absolute system path
echo "Patching LiveKit RPATHs..."
patchelf --set-rpath '$ORIGIN' AppDir/usr/lib/liblivekit.so
patchelf --set-rpath '$ORIGIN' AppDir/usr/lib/liblivekit_ffi.so

patchelf --replace-needed \
    /opt/livekit-cpp/build/lib/liblivekit_ffi.so \
    liblivekit_ffi.so \
    AppDir/usr/lib/liblivekit.so

patchelf --replace-needed \
    /opt/livekit-cpp/build/lib/liblivekit_ffi.so \
    liblivekit_ffi.so \
    AppDir/usr/bin/direct-link
patchelf --set-rpath '$ORIGIN/../lib' AppDir/usr/bin/direct-link

echo "Packaging AppImage..."
"$LINUXDEPLOY" \
    --appdir AppDir \
    --executable AppDir/usr/bin/direct-link \
    --desktop-file deploy/linux/direct-link.desktop \
    --icon-file deploy/linux/direct-link.png \
    --custom-apprun deploy/linux/AppRun \
    --plugin qt \
    --output appimage
