#!/usr/bin/env bash
# Run the E2E latency test against GKE and bundle the results for sharing.
#
# Usage:
#   ./scripts/run-latency-test.sh                 # default: GKE, 1800 samples (~80s)
#   ./scripts/run-latency-test.sh --samples 30    # quick smoke test (~25s)
#   ./scripts/run-latency-test.sh --server http://<host>:50051
#
# Output: a single .tar.gz file in the repo root containing:
#   - run.log              full stderr+stdout from the test (qDebug enabled)
#   - stats.txt            just the final [Stats] table, easy to eyeball
#   - sysinfo.txt          OS / CPU / kernel / camera hardware
#   - netinfo.txt          default route, link speed, ping to the signaling host
#
# Why 1800 samples by default: latency on this stack is dominated by adaptive
# jitter buffers in libwebrtc / LiveKit Ingress, which take ~30–60 s of clean
# samples to converge.  Empirically post-settling sample rate is ~30/s, so
# 1800 samples ≈ 60 s of converged data on top of ~20 s of setup.  Anything
# under a few hundred samples captures only the inflated initial state and
# misses the steady-state floor that's actually informative.
#
# Send the .tar.gz back. That's everything needed to compare your run against
# someone else's (the per-sample diagnostics in run.log let us see whether
# jitter_buffer / upstream_vid trend down over time).

# `set -e` and `pipefail` are intentionally NOT used.  The sysinfo and
# netinfo gathering blocks call optional tools (ethtool, iwconfig, nvidia-smi,
# v4l2-ctl) that are commonly missing or non-zero-exit, and we want best-effort
# output rather than aborting the script.  The test run itself is exit-checked
# explicitly below.  -u still catches unbound-variable typos.
set -u

SERVER="http://34.174.71.83:50051"
SAMPLES=1800

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server)  SERVER="$2"; shift 2 ;;
        --samples) SAMPLES="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,18p' "$0" | sed 's/^# //; s/^#//'
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Run with --help for usage." >&2
            exit 2
            ;;
    esac
done

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${REPO_ROOT}/client/build/test_e2e_latency"

if [[ ! -x "${BIN}" ]]; then
    echo "test_e2e_latency binary not found at ${BIN}." >&2
    echo "Build it first:" >&2
    echo "  cmake --build ${REPO_ROOT}/client/build --target test_e2e_latency" >&2
    exit 1
fi

if [[ ! -e /dev/video0 ]]; then
    echo "/dev/video0 not present — the test needs a real camera." >&2
    exit 1
fi

TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
HOSTNAME_TAG="$(hostname -s 2>/dev/null || echo unknown)"
WORKDIR="$(mktemp -d -t latency-test-XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

echo "==> Run started at ${TIMESTAMP}"
echo "    server:   ${SERVER}"
echo "    samples:  ${SAMPLES}"
echo "    workdir:  ${WORKDIR}"
echo

# System info — useful for telling apart machines, kernels, encoders.
{
    echo "# sysinfo (${TIMESTAMP})"
    echo "host:        $(hostname 2>/dev/null || echo unknown)"
    echo "kernel:      $(uname -srm)"
    echo "os-release:  $(. /etc/os-release 2>/dev/null && echo "${PRETTY_NAME:-unknown}")"
    echo "cpu:        $(grep -m1 '^model name' /proc/cpuinfo 2>/dev/null | sed 's/^model name\s*:\s*//')"
    echo "cores:       $(nproc 2>/dev/null || echo unknown)"
    echo "mem-total:   $(grep -m1 MemTotal /proc/meminfo 2>/dev/null | awk '{print $2, $3}')"
    echo
    echo "# camera"
    if command -v v4l2-ctl >/dev/null 2>&1; then
        v4l2-ctl --device=/dev/video0 --info 2>/dev/null || echo "v4l2-ctl --info failed"
    else
        ls -l /dev/video* 2>/dev/null
        echo "(install v4l2-ctl for richer camera info)"
    fi
    echo
    echo "# gpu (NVENC availability)"
    if command -v nvidia-smi >/dev/null 2>&1; then
        nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null \
            || echo "nvidia-smi failed"
    else
        echo "no nvidia-smi (NVENC unavailable; encoder will fall back to VAAPI/software)"
    fi
} > "${WORKDIR}/sysinfo.txt"

# Network info — link type and a baseline RTT to the signaling host. The
# signaling host is the same gRPC URL the test uses, so its ping is comparable
# to what the camera/director loop will see.
SIGNALING_HOST="$(echo "${SERVER}" | sed -E 's|^https?://||; s|:.*||')"
{
    echo "# netinfo (${TIMESTAMP})"
    echo "signaling-url:  ${SERVER}"
    echo "signaling-host: ${SIGNALING_HOST}"
    echo
    echo "# default route"
    ip route show default 2>/dev/null || route -n 2>/dev/null | head -3
    echo
    echo "# active interfaces"
    if command -v ip >/dev/null 2>&1; then
        ip -brief link show up 2>/dev/null
        echo
        echo "# link speed (ethtool, may need root)"
        for iface in $(ip -brief link show up | awk '$1!="lo"{print $1}'); do
            # Sysfs is the reliable signal: a `wireless` subdir is created by
            # the kernel for any WiFi interface, regardless of whether iwconfig
            # / iw is installed (Fedora 43 ships neither by default).
            if [[ -d "/sys/class/net/${iface}/wireless" ]]; then
                ssid="$(iw dev "${iface}" link 2>/dev/null | awk '/SSID/{print $2; exit}')"
                bitrate="$(iw dev "${iface}" link 2>/dev/null | awk -F': ' '/tx bitrate/{print $2; exit}')"
                echo "${iface}: WIRELESS  ssid=${ssid:-?}  tx_bitrate=${bitrate:-?}"
            else
                speed="$(ethtool "${iface}" 2>/dev/null | awk -F': ' '/Speed/{print $2; exit}')"
                duplex="$(ethtool "${iface}" 2>/dev/null | awk -F': ' '/Duplex/{print $2; exit}')"
                if [[ -n "${speed}" ]]; then
                    echo "${iface}: WIRED  ${speed} ${duplex}"
                else
                    echo "${iface}: WIRED  (ethtool unavailable or denied)"
                fi
            fi
        done
    fi
    echo
    echo "# ping to signaling (10 packets)"
    ping -c 10 -i 0.2 -W 2 "${SIGNALING_HOST}" 2>&1 | tail -3 || echo "ping failed"
} > "${WORKDIR}/netinfo.txt"

# The test itself.  Qt's qDebug() is fully buffered when stderr is a pipe;
# stdbuf forces line-buffering so partial logs survive a kill.  The Qt logging
# rules unmute every category except Qt internals — without this, sample lines
# get filtered by the system qtlogging.ini.
# Empirical: setup (room/livekit/whip/clock-sync) ≈ 15–20 s, then samples
# land at ~8–10/s on GKE WAN paths (frames arrive faster but the matcher
# is rate-limited by network timing).  20 + samples/8 covers slow GKE
# warmup with a small margin.
EST_SECONDS=$(( SAMPLES / 8 + 20 ))
echo "==> Running test (estimated ~${EST_SECONDS}s for ${SAMPLES} samples)..."
START_NS="$(date +%s%N)"
set +e
QT_FORCE_STDERR_LOGGING=1 \
QT_LOGGING_RULES="*.debug=true;qt.*=false" \
stdbuf -oL -eL "${BIN}" --server "${SERVER}" --samples "${SAMPLES}" \
    > "${WORKDIR}/run.log" 2>&1
TEST_EXIT=$?
set -e
END_NS="$(date +%s%N)"
DURATION_S=$(( (END_NS - START_NS) / 1000000000 ))

echo "==> Test exited with code ${TEST_EXIT} after ${DURATION_S}s"

# Pull the [Stats] block (final summary) into its own file for quick eyeballing.
# If no [Stats] header exists, the test failed before printing — surface that
# clearly so a recipient knows the bundle is from a failed run.
if grep -q '^\[Stats\]' "${WORKDIR}/run.log"; then
    awk '/^\[Stats\]/{p=1} p' "${WORKDIR}/run.log" \
        | head -20 > "${WORKDIR}/stats.txt"
else
    {
        echo "# stats.txt — no [Stats] line found in run.log"
        echo "# the test exited with code ${TEST_EXIT} after ${DURATION_S}s"
        echo "# without producing a final summary.  See run.log for details."
        echo
        grep -E "qCritical|Failed to|Only [0-9]+ /|Critical|Segmentation|Aborted" \
            "${WORKDIR}/run.log" | head -20 || true
    } > "${WORKDIR}/stats.txt"
fi

# Bundle.  Name carries timestamp + host so multiple bundles don't collide
# when several people send theirs in.
BUNDLE="${REPO_ROOT}/latency-${TIMESTAMP}-${HOSTNAME_TAG}.tar.gz"
tar -czf "${BUNDLE}" -C "${WORKDIR}" run.log stats.txt sysinfo.txt netinfo.txt

echo
echo "==> stats.txt:"
sed 's/^/    /' "${WORKDIR}/stats.txt"
echo
echo "==> Bundle written:"
echo "    ${BUNDLE}"
echo "    ($(du -h "${BUNDLE}" | cut -f1))"
echo
echo "Send this .tar.gz back."

exit "${TEST_EXIT}"
