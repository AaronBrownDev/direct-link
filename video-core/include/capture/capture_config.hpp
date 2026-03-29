#pragma once

#include <string>

namespace videoCore::capture {
struct CaptureConfig {
    std::string devicePath =
        "/dev/video0";                // Linux: /dev/video0, Windows: "video=0"
    std::string inputFormat = "v4l2"; // Linux: v4l2, Windows: dshow
    // Pixel format to negotiate with the device driver (e.g. "mjpeg", "yuyv422").
    // Most USB cameras only sustain 30 fps at 720p+ in MJPEG; leaving this empty
    // lets the driver pick a format that may cap the frame rate at 10 fps.
    std::string pixelFormat;
    int width = 0;
    int height = 0;
    int framerate = 30;
    int bufferSize = 4;
};
} // namespace videoCore::capture