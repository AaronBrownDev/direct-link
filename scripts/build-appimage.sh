#!/usr/bin/env bash
# Build the direct-link client as a Linux AppImage.
#
# Usage: ./scripts/build-appimage.sh [version]
#
# Environment overrides:
#   LIVEKIT_DIR      Path to livekit-cpp (default: $HOME/livekit-cpp)
#   QT_PREFIX        Qt6 install prefix    (auto-detected if unset)
#   BUILD_DIR        CMake build directory (default: client/build-appimage)
#   DIST_DIR         Output directory      (default: dist/)
#   JOBS             Parallel build jobs   (default: nproc)
#   TOOLS_DIR        linuxdeploy cache dir (default: $HOME/.cache/linuxdeploy)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CLIENT_DIR="${REPO_ROOT}/client"

VERSION="${1:-${VERSION:-dev}}"
BUILD_DIR="${BUILD_DIR:-${CLIENT_DIR}/build-appimage}"
DIST_DIR="${DIST_DIR:-${REPO_ROOT}/dist}"
LIVEKIT_DIR="${LIVEKIT_DIR:-${HOME}/livekit-cpp}"
JOBS="${JOBS:-$(nproc)}"
TOOLS_DIR="${TOOLS_DIR:-${HOME}/.cache/linuxdeploy}"

APPDIR="${BUILD_DIR}/AppDir"
APP_NAME="direct-link"
APP_DISPLAY_NAME="Direct Link"
APPIMAGE_OUT="${DIST_DIR}/${APP_NAME}-${VERSION}-x86_64.AppImage"

LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_GST_URL="https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gstreamer/master/linuxdeploy-plugin-gstreamer.sh"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m==>\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m==>\033[0m %s\n' "$*" >&2; exit 1; }

require() {
    command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"
}

require cmake
require ninja
require curl
require pkg-config
require patchelf  # used by linuxdeploy-plugin-gstreamer

[[ -d "${LIVEKIT_DIR}" ]] || die "LIVEKIT_DIR does not exist: ${LIVEKIT_DIR}"
[[ -f "${LIVEKIT_DIR}/build/lib/liblivekit.so" ]] \
    || die "liblivekit.so not found at ${LIVEKIT_DIR}/build/lib/. Build livekit-cpp first."

# Auto-detect Qt prefix from qmake6 if QT_PREFIX is unset.
if [[ -z "${QT_PREFIX:-}" ]]; then
    if command -v qmake6 >/dev/null 2>&1; then
        QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX)"
    elif command -v qmake >/dev/null 2>&1; then
        QT_PREFIX="$(qmake -query QT_INSTALL_PREFIX)"
    else
        die "Qt6 not found. Install Qt6 or set QT_PREFIX."
    fi
fi
log "Qt prefix: ${QT_PREFIX}"

# ---------------------------------------------------------------------------
# Fetch linuxdeploy + plugins (cached)
# ---------------------------------------------------------------------------
mkdir -p "${TOOLS_DIR}"

fetch_tool() {
    local url="$1" dest="$2"
    if [[ ! -x "${dest}" ]]; then
        log "Fetching $(basename "${dest}")"
        curl -fL --retry 3 -o "${dest}" "${url}"
        chmod +x "${dest}"
    fi
}

LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="${TOOLS_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_GST="${TOOLS_DIR}/linuxdeploy-plugin-gstreamer.sh"

fetch_tool "${LINUXDEPLOY_URL}"     "${LINUXDEPLOY}"
fetch_tool "${LINUXDEPLOY_QT_URL}"  "${LINUXDEPLOY_QT}"
fetch_tool "${LINUXDEPLOY_GST_URL}" "${LINUXDEPLOY_GST}"

# ---------------------------------------------------------------------------
# Configure + build
# ---------------------------------------------------------------------------
log "Configuring (Release) in ${BUILD_DIR}"
cmake -S "${CLIENT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX}" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DLIVEKIT_DIR="${LIVEKIT_DIR}"

log "Building direct-link with ${JOBS} jobs"
cmake --build "${BUILD_DIR}" --target "${APP_NAME}" -j "${JOBS}"

# ---------------------------------------------------------------------------
# Stage AppDir
# ---------------------------------------------------------------------------
log "Staging AppDir at ${APPDIR}"
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib"

# Copy the built executable directly. We don't use `cmake --install` because
# the umbrella install pulls in transitive subproject install rules
# (e.g. video-core's encode_test) that aren't built by our targeted build.
EXE_PATH="$(find "${BUILD_DIR}" -maxdepth 4 -type f -name "${APP_NAME}" -executable | head -1)"
[[ -n "${EXE_PATH}" ]] || die "${APP_NAME} binary not found under ${BUILD_DIR}"
install -Dm755 "${EXE_PATH}" "${APPDIR}/usr/bin/${APP_NAME}"

# liblivekit_ffi.so is a Rust cdylib without a SONAME, so the linker bakes the
# absolute build-time path into the consuming binary's NEEDED entry, bypassing
# RUNPATH. Rewrite any absolute-path NEEDED entries to bare basenames so they
# resolve via $ORIGIN/../lib at runtime.
while IFS= read -r needed; do
    [[ "${needed}" == /* ]] || continue
    base="$(basename "${needed}")"
    log "patching NEEDED: ${needed} -> ${base}"
    patchelf --replace-needed "${needed}" "${base}" "${APPDIR}/usr/bin/${APP_NAME}"
done < <(patchelf --print-needed "${APPDIR}/usr/bin/${APP_NAME}")

# LiveKit ships as standalone .so files; place them where linuxdeploy will
# pick them up and patch RPATH alongside the rest of the bundled libs.
cp "${LIVEKIT_DIR}/build/lib/liblivekit.so"     "${APPDIR}/usr/lib/"
cp "${LIVEKIT_DIR}/build/lib/liblivekit_ffi.so" "${APPDIR}/usr/lib/"
if [[ -f "${LIVEKIT_DIR}/build/lib/liblivekit_bridge.so" ]]; then
    cp "${LIVEKIT_DIR}/build/lib/liblivekit_bridge.so" "${APPDIR}/usr/lib/"
fi

# Icon: AppImage requires a top-level icon matching the .desktop Icon= entry.
ICON_SRC="${CLIENT_DIR}/resources/icons/cast.png"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"
cp "${ICON_SRC}" "${APPDIR}/${APP_NAME}.png"

# Desktop entry
mkdir -p "${APPDIR}/usr/share/applications"
cat > "${APPDIR}/usr/share/applications/${APP_NAME}.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=${APP_DISPLAY_NAME}
GenericName=Camera-to-display latency
Comment=Direct Link client
Exec=${APP_NAME}
Icon=${APP_NAME}
Terminal=false
Categories=AudioVideo;
EOF

# ---------------------------------------------------------------------------
# Run linuxdeploy
# ---------------------------------------------------------------------------
log "Running linuxdeploy (Qt + GStreamer plugins)"

# linuxdeploy-plugin-qt invokes qmake to discover plugin/QML deployment paths.
if [[ -x "${QT_PREFIX}/bin/qmake6" ]]; then
    export QMAKE="${QT_PREFIX}/bin/qmake6"
elif [[ -x "${QT_PREFIX}/bin/qmake" ]]; then
    export QMAKE="${QT_PREFIX}/bin/qmake"
else
    die "qmake not found under ${QT_PREFIX}/bin"
fi

# Output naming for the appimage output plugin.
export LDAI_OUTPUT="${APPIMAGE_OUT}"
export LINUXDEPLOY_OUTPUT_VERSION="${VERSION}"

# Help linuxdeploy resolve transitive deps of the binary it's scanning.
#
# The system lib path is prepended so that libavcodec.so.61 resolves to the
# distro's libavcodec-extra rather than Qt 6's bundled FFmpeg.  Qt ships an
# LGPL-only libavcodec at ${QT_PREFIX}/lib that lacks libx264 / libopenh264
# codec wrappers, which breaks the software encoder fallback used when
# NVENC isn't available at runtime (e.g. on a host without an NVIDIA GPU).
# Qt's libraries use DT_RUNPATH (not DT_RPATH), so LD_LIBRARY_PATH takes
# precedence over their $ORIGIN search.
SYSTEM_LIB_DIR="$(dirname "$(realpath "$(ldconfig -p | grep 'libc.so.6 ' | awk '{print $4}' | head -1)")")"
export LD_LIBRARY_PATH="${SYSTEM_LIB_DIR}:${LIVEKIT_DIR}/build/lib:${QT_PREFIX}/lib:${LD_LIBRARY_PATH:-}"

# Tell the Qt plugin where the QML source tree lives so QML imports get scanned.
export QML_SOURCES_PATHS="${CLIENT_DIR}/src"

# Locally-built QML modules (qt_add_qml_module) emit qmldir + types under the
# build tree. Point qmlimportscanner at them so linuxdeploy can resolve and
# bundle them; without this, project-internal modules fail with
# "ERROR: Missing qml module: <uri>".
export QML_IMPORT_PATH="${BUILD_DIR}/src"

# binutils bundled inside linuxdeploy is too old to parse SHT_RELR sections
# produced by modern toolchains (-z,pack-relative-relocs); skip stripping.
export NO_STRIP=1

# Pin the architecture so appimagetool doesn't get confused by multilib stubs
# present in /usr/lib on most distros.
export ARCH=x86_64

# linuxdeploy and its plugins are AppImages themselves; in environments without
# /dev/fuse (containers, CI runners) they fail to self-mount. Extract instead.
export APPIMAGE_EXTRACT_AND_RUN=1

mkdir -p "${DIST_DIR}"

"${LINUXDEPLOY}" \
    --appdir "${APPDIR}" \
    --plugin qt \
    --plugin gstreamer \
    --desktop-file "${APPDIR}/usr/share/applications/${APP_NAME}.desktop" \
    --icon-file "${APPDIR}/${APP_NAME}.png" \
    --output appimage

[[ -f "${APPIMAGE_OUT}" ]] || die "linuxdeploy did not produce ${APPIMAGE_OUT}"

log "Built: ${APPIMAGE_OUT}"
ls -lh "${APPIMAGE_OUT}"
