/*
 * latency_overlay.hpp — benchmark-mode video overlay for ground-truth
 * end-to-end latency measurement.
 *
 * The camera-side caller embeds a 64-bit value (typically a server-domain
 * timestamp) into the top-left 128x128 region of a captured YUV420 frame
 * as an 8x8 grid of 16x16 black/white cells.  The receiver decodes the
 * cells, recovers the value, and (knowing its own clock offset to the
 * same server) computes the wall-clock interval since the camera-side
 * stamp.
 *
 * This bypasses the DC matcher entirely: the timestamp rides inside the
 * video pixels through every element of the pipeline (encode, WHIP,
 * Ingress, SFU, libwebrtc jitter buffer, decode), so the measurement
 * captures the genuine end-to-end latency including server-side buffers
 * that no client-side instrumentation can see.
 *
 * 16x16 cells survive H.264 DCT quantization at any reasonable bitrate.
 * The decoder samples the center 8x8 of each cell to ignore DCT bleed at
 * cell boundaries, and requires at least 90% of cells to be clearly
 * black or clearly white before accepting the decode — random scene
 * content will reliably fail this gate.
 *
 * Visual artifact: a 128x128 black/white pattern in the top-left corner
 * of every frame.  Use only in benchmark mode (`--benchmark-latency`),
 * never in production.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

// Forward-declare AVFrame so callers that only need the benchmark flag
// setters or the (uint8-buffer-based) decode function don't need to drag
// libav into their include path.  The actual draw implementation lives
// in latency_overlay.cpp where libavutil/frame.h is available.
struct AVFrame;

namespace videoCore {

// Geometry of the overlay region.  Both sides agree on these constants;
// changing them would break decoding cross-version.
inline constexpr int LATENCY_OVERLAY_CELL_SIZE = 16;
inline constexpr int LATENCY_OVERLAY_GRID = 8; // 8x8 cells = 64 bits
inline constexpr int LATENCY_OVERLAY_REGION =
    LATENCY_OVERLAY_CELL_SIZE * LATENCY_OVERLAY_GRID; // 128 pixels

// Draw a 64-bit value into the top-left LATENCY_OVERLAY_REGION square of
// the frame's Y plane.  Bit `i` is rendered as cell (row=i/8, col=i%8)
// — fully white (Y=255) if set, fully black (Y=0) otherwise.  No-op
// when the frame is null or smaller than the overlay region.
// Defined in latency_overlay.cpp (touches AVFrame internals).
void drawTimestampOverlay(::AVFrame *frame, std::uint64_t value) noexcept;

// Decode the overlay from a Y-plane buffer.  Returns true and writes the
// 64-bit value to `out` when at least 90% of the cells are clearly black
// or clearly white (the high-contrast gate that rejects normal scene
// content).  Returns false otherwise — caller should treat as "no
// benchmark overlay present" and skip emitting a latency reading.
//
// `y_plane` may be a pointer into a libwebrtc frame's Y buffer (I420)
// or a libav AVFrame; the caller supplies the stride.
inline bool decodeTimestampOverlay(const std::uint8_t *y_plane, int stride,
                                   int width, int height,
                                   std::uint64_t *out) noexcept {
    if (y_plane == nullptr || out == nullptr) { return false; }
    if (width < LATENCY_OVERLAY_REGION || height < LATENCY_OVERLAY_REGION) {
        return false;
    }
    // Sample the center 8x8 of each 16x16 cell — skipping a 4-pixel
    // border avoids DCT bleed at the cell boundary and reduces decode
    // errors when the encoder used a low bitrate.
    constexpr int SAMPLE_MARGIN = 4;
    constexpr int SAMPLE_SIZE = LATENCY_OVERLAY_CELL_SIZE - 2 * SAMPLE_MARGIN;
    constexpr int SAMPLES_PER_CELL = SAMPLE_SIZE * SAMPLE_SIZE; // 64
    constexpr int LOW_THRESHOLD = 64;
    constexpr int HIGH_THRESHOLD = 191;
    constexpr int TOTAL_CELLS = LATENCY_OVERLAY_GRID * LATENCY_OVERLAY_GRID;
    constexpr int MIN_HIGH_CONTRAST_CELLS = (TOTAL_CELLS * 9) / 10; // 57

    std::uint64_t value = 0;
    int high_contrast = 0;
    for (int row = 0; row < LATENCY_OVERLAY_GRID; ++row) {
        for (int col = 0; col < LATENCY_OVERLAY_GRID; ++col) {
            int sum = 0;
            const int cell_y = row * LATENCY_OVERLAY_CELL_SIZE + SAMPLE_MARGIN;
            const int cell_x = col * LATENCY_OVERLAY_CELL_SIZE + SAMPLE_MARGIN;
            for (int dy = 0; dy < SAMPLE_SIZE; ++dy) {
                const std::uint8_t *line = y_plane +
                    static_cast<std::ptrdiff_t>(cell_y + dy) * stride + cell_x;
                for (int dx = 0; dx < SAMPLE_SIZE; ++dx) {
                    sum += line[dx];
                }
            }
            const int avg = sum / SAMPLES_PER_CELL;
            const bool bit = avg > 128;
            if (avg <= LOW_THRESHOLD || avg >= HIGH_THRESHOLD) {
                ++high_contrast;
            }
            const int bit_index = row * LATENCY_OVERLAY_GRID + col;
            if (bit) {
                value |= (1ULL << bit_index);
            }
        }
    }
    if (high_contrast < MIN_HIGH_CONTRAST_CELLS) {
        return false;
    }
    *out = value;
    return true;
}

namespace benchmark {

// Process-wide flag controlling whether the overlay is drawn on captured
// frames and decoded on received frames.  Set by main() based on a
// command-line argument; default false.  Reads/writes are relaxed-atomic
// — the value is only checked once per frame and a torn write at startup
// is harmless.
inline std::atomic<bool> &latencyOverlayEnabledFlag() noexcept {
    static std::atomic<bool> flag{false};
    return flag;
}
inline void setLatencyOverlayEnabled(bool enabled) noexcept {
    latencyOverlayEnabledFlag().store(enabled, std::memory_order_relaxed);
}
[[nodiscard]] inline bool isLatencyOverlayEnabled() noexcept {
    return latencyOverlayEnabledFlag().load(std::memory_order_relaxed);
}

} // namespace benchmark
} // namespace videoCore
