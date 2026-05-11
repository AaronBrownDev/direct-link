#include "../../include/common/latency_overlay.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
}

namespace videoCore {

void drawTimestampOverlay(::AVFrame *frame, std::uint64_t value) noexcept {
    if (frame == nullptr) { return; }
    if (frame->width < LATENCY_OVERLAY_REGION ||
        frame->height < LATENCY_OVERLAY_REGION) {
        return;
    }
    std::uint8_t *y = frame->data[0];
    if (y == nullptr) { return; }
    const int stride = frame->linesize[0];
    for (int row = 0; row < LATENCY_OVERLAY_GRID; ++row) {
        for (int col = 0; col < LATENCY_OVERLAY_GRID; ++col) {
            const int bit = (row * LATENCY_OVERLAY_GRID) + col;
            const std::uint8_t fill =
                ((value >> bit) & 1ULL) != 0 ? 255 : 0;
            for (int dy = 0; dy < LATENCY_OVERLAY_CELL_SIZE; ++dy) {
                std::uint8_t *line = y +
                    static_cast<std::ptrdiff_t>((row * LATENCY_OVERLAY_CELL_SIZE) + dy) * stride +
                    (col * LATENCY_OVERLAY_CELL_SIZE);
                std::memset(line, fill, LATENCY_OVERLAY_CELL_SIZE);
            }
        }
    }
}

} // namespace videoCore
