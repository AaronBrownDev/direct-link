#pragma once
#include <cstdint>
#include <string>

namespace videoCore::encode {
struct EncoderConfig {
    int width = 0;
    int height = 0;
    int bitrate = 4000000; // 4 Mbps
    int framerate = 30;
    int gopSize = 60; // Keyframe every 2 seconds at 30 fps

    enum class Preset : std::uint8_t {
        UltraFast, // Default for low-latency streaming
        Fast,
        Medium,
        Slow,
    } preset = Preset::UltraFast;

    enum class Type : std::uint8_t {
        Software, // CPU encoding (libx264)
        NVENC,    // NVIDIA GPU (h264_nvenc) — needs libcuda.so.1
        VAAPI,    // AMD / Intel GPU (h264_vaapi) — needs libva + DRI render node
        // Backwards-compatible alias.  Pre-VAAPI code referred to the
        // hardware encoder as "Hardware"; that meant NVENC at the time.
        Hardware = NVENC,
    } type = Type::Software;
};
} // namespace videoCore::encode