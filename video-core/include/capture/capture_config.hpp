#pragma once

#include <string>

namespace videoCore::capture {
struct CaptureConfig {
    std::string devicePath =
        "/dev/video0";                // Linux: /dev/video0, Windows: "video=0"
    std::string inputFormat = "v4l2"; // Linux: v4l2, Windows: dshow
    std::string pixelFormat;          // e.g. "mjpeg", "yuyv422"
    int width = 0;
    int height = 0;
    int framerate = 30;
    int bufferSize = 4;
};
} // namespace videoCore::capture