#!/usr/bin/env bash
# benchmark-latency.sh — repeated ground-truth latency benchmarking.
#
# Runs the e2e test N times with --benchmark-latency, extracts the overlay
# readings from each bundle's run.log, and reports:
#   - per-iteration stats (mean/median/p95/p99/std/min/max, into CSV)
#   - overall aggregate stats across every sample from every iteration
#   - session-to-session variance: stddev of per-iteration means
#
# Per-iteration stats answer "what's a typical latency in one session?"
# Aggregate stats answer "what's the true latency of this stack?"
# Session-to-session variance answers "is my measurement repeatable, or
# does each session vary because of network/Ingress/libwebrtc state?"
#
# Usage:
#   ./scripts/benchmark-latency.sh                       # 10 x 1000 samples
#   ./scripts/benchmark-latency.sh --iterations 100
#   ./scripts/benchmark-latency.sh --samples 2000
#   ./scripts/benchmark-latency.sh --keep-bundles        # don't delete after parse
#   ./scripts/benchmark-latency.sh --out results.csv
#
# Skips the first SKIP_HEAD overlay readings per iteration to avoid the
# DTLS-handshake / libwebrtc-jitter-buffer-warmup outliers that show up
# in the first ~5 seconds.  Default is 200 (~7 s at 30 fps).
#
# Time budget: ~(samples/8 + 25) seconds per iteration, dominated by
# WebRTC session setup + the configured sample count.

set -u

ITERATIONS=10
SAMPLES=1000
SERVER="http://34.174.71.83:50051"
KEEP_BUNDLES=0
SKIP_HEAD=200
OUT="benchmark-results.csv"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iterations)   ITERATIONS="$2"; shift 2 ;;
        --samples)      SAMPLES="$2"; shift 2 ;;
        --server)       SERVER="$2"; shift 2 ;;
        --skip-head)    SKIP_HEAD="$2"; shift 2 ;;
        --keep-bundles) KEEP_BUNDLES=1; shift ;;
        --out)          OUT="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,28p' "$0" | sed 's/^# //; s/^#//'
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
BUILD_DIR="${REPO_ROOT}/client/build"

# Rebuild test_e2e_latency once up front.  The per-iteration script also
# tries to rebuild but we skip that via --no-rebuild to save time.
echo "==> Pre-build of test_e2e_latency..."
if ! cmake --build "${BUILD_DIR}" --target test_e2e_latency -j; then
    echo "Build failed; aborting." >&2
    exit 1
fi

echo
echo "==> Benchmark: ${ITERATIONS} iterations x ${SAMPLES} samples each"
echo "    server:           ${SERVER}"
echo "    skip head:        ${SKIP_HEAD} overlay readings per iteration"
echo "    output CSV:       ${OUT}"
echo "    keep bundles:     ${KEEP_BUNDLES}"
echo

# Per-iteration CSV header.
echo "iter,n,mean,median,p95,p99,stddev,min,max" > "${OUT}"

# Combined readings across all iterations (for aggregate stats).
ALL_READINGS="$(mktemp)"
PER_ITER_LOG="$(mktemp)"
cleanup() { rm -f "${ALL_READINGS}" "${PER_ITER_LOG}"; }
trap cleanup EXIT

# Print aggregate stats on Ctrl+C so a partial run still gives data.
report_aggregate() {
    local readings_file="$1"
    if [[ ! -s "${readings_file}" ]]; then
        echo "==> No overlay readings captured."
        return
    fi
    local count
    count="$(wc -l < "${readings_file}")"
    echo
    echo "==> Aggregate over all iterations (${count} readings):"
    awk '
        { s+=$1; sq+=$1*$1; c++; a[c]=$1 }
        END {
            n=asort(a);
            mean=s/c; var=(sq/c) - mean*mean;
            sd = var > 0 ? sqrt(var) : 0;
            printf "    N         %d\n", c;
            printf "    mean      %.2f ms\n", mean;
            printf "    median    %.2f ms\n", a[int(n*0.5)];
            printf "    p95       %.2f ms\n", a[int(n*0.95)];
            printf "    p99       %.2f ms\n", a[int(n*0.99)];
            printf "    stddev    %.2f ms\n", sd;
            printf "    min       %.2f ms\n", a[1];
            printf "    max       %.2f ms\n", a[n];
        }
    ' "${readings_file}"
}

report_session_variance() {
    local csv="$1"
    if [[ "$(wc -l < "${csv}")" -le 1 ]]; then
        return
    fi
    echo
    echo "==> Session-to-session variance (mean per iteration):"
    awk -F, 'NR>1 { s+=$3; sq+=$3*$3; c++; a[c]=$3 } END {
        if (c < 2) {
            printf "    only %d iteration(s); need >=2 for variance\n", c;
            exit;
        }
        mean=s/c; var=(sq/c) - mean*mean;
        sd = var > 0 ? sqrt(var) : 0;
        printf "    iterations:                %d\n", c;
        printf "    mean of per-iter means:    %.2f ms\n", mean;
        printf "    stddev of per-iter means:  %.2f ms\n", sd;
        printf "    range:                     %.2f ms\n", (sd > 0 ? sd*4 : 0);
    }' "${csv}"
}

on_interrupt() {
    echo
    echo "==> Interrupted.  Reporting partial results so far..."
    report_aggregate "${ALL_READINGS}"
    report_session_variance "${OUT}"
    echo
    echo "==> CSV written: ${OUT}"
    exit 130
}
trap on_interrupt INT

OVERALL_START="$(date +%s)"

# Discover newest existing bundle so the "newest after run" detection
# below can reliably pick up THIS iteration's bundle.
BUNDLE_GLOB="${REPO_ROOT}/latency-*.tar.gz"

for i in $(seq 1 "${ITERATIONS}"); do
    ITER_START="$(date +%s)"
    echo "==> Iteration ${i}/${ITERATIONS}..."

    # Snapshot bundles present BEFORE this iteration so we can identify
    # the new one afterwards even if other bundles linger on disk.
    BEFORE_BUNDLES="$(ls -1 ${BUNDLE_GLOB} 2>/dev/null | sort -u || true)"

    # Run.  --no-rebuild so we don't burn time on a cmake check each time.
    # All run-script output goes to PER_ITER_LOG; we tail the bundle path
    # from there.
    set +e
    "${REPO_ROOT}/scripts/run-latency-test.sh" \
        --no-rebuild \
        --samples "${SAMPLES}" \
        --server "${SERVER}" \
        --benchmark-latency \
        > "${PER_ITER_LOG}" 2>&1
    RUN_EXIT=$?
    set -e

    AFTER_BUNDLES="$(ls -1 ${BUNDLE_GLOB} 2>/dev/null | sort -u || true)"
    NEW_BUNDLE="$(comm -13 <(echo "${BEFORE_BUNDLES}") <(echo "${AFTER_BUNDLES}") | head -1)"

    if [[ -z "${NEW_BUNDLE}" || ! -f "${NEW_BUNDLE}" ]]; then
        echo "    ⚠ no bundle produced (run-script exit ${RUN_EXIT}); skipping iter ${i}"
        continue
    fi
    if [[ ${RUN_EXIT} -ne 0 ]]; then
        echo "    ⚠ run-script exited ${RUN_EXIT} but bundle exists; parsing anyway"
    fi

    # Extract overlay readings from this iteration's bundle, drop the
    # first SKIP_HEAD entries (DTLS/JB warmup outliers).
    TMPDIR="$(mktemp -d)"
    tar -xzf "${NEW_BUNDLE}" -C "${TMPDIR}"
    READINGS="$(grep '\[benchmark\] overlay' "${TMPDIR}/run.log" 2>/dev/null \
        | awk -F'capture_to_receive_ms=' '{print $2}' \
        | awk '{print $1}' \
        | tail -n +$((SKIP_HEAD + 1)))"

    if [[ -z "${READINGS}" ]]; then
        echo "    ⚠ no overlay readings (skip-head=${SKIP_HEAD} ate everything? or benchmark disabled in the bundle); skipping"
        rm -rf "${TMPDIR}"
        if [[ ${KEEP_BUNDLES} -eq 0 ]]; then
            rm -f "${NEW_BUNDLE}"
        fi
        continue
    fi

    echo "${READINGS}" >> "${ALL_READINGS}"

    # Per-iteration stats (one CSV row).
    STATS="$(echo "${READINGS}" | awk -v iter="${i}" '
        { s+=$1; sq+=$1*$1; c++; a[c]=$1 }
        END {
            n=asort(a);
            mean=s/c; var=(sq/c) - mean*mean;
            sd = var > 0 ? sqrt(var) : 0;
            printf "%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
                iter, c, mean, a[int(n*0.5)], a[int(n*0.95)],
                a[int(n*0.99)], sd, a[1], a[n];
        }')"
    echo "${STATS}" >> "${OUT}"

    ITER_END="$(date +%s)"
    ITER_SEC=$((ITER_END - ITER_START))
    echo "    ${STATS}  (${ITER_SEC}s)"

    rm -rf "${TMPDIR}"
    if [[ ${KEEP_BUNDLES} -eq 0 ]]; then
        rm -f "${NEW_BUNDLE}"
    fi
done

OVERALL_END="$(date +%s)"
TOTAL_SEC=$((OVERALL_END - OVERALL_START))
echo
echo "==> All iterations complete (${TOTAL_SEC}s elapsed)"

report_aggregate "${ALL_READINGS}"
report_session_variance "${OUT}"

echo
echo "==> CSV written: ${OUT}"
