#pragma once
#include <string>

namespace videoCore::encode {
struct EncoderConfig {
    int width = 0;
    int height = 0;
    int bitrate = 4000000; // 4 Mbps
    int framerate = 30;
    int gopSize = 60; // Keyframe every 2 seconds at 30 fps

    enum class Preset {
        UltraFast, // Default for low-latency streaming
        Fast,
        Medium,
        Slow,
    } preset = Preset::UltraFast;

    enum class Type {
        Software, // Use CPU-based encoding (x264)
        Hardware, // Use GPU-based encoding (NVENC)
    } type = Type::Software;
};
}