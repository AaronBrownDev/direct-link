#pragma once

#include <string>

namespace videoCore::capture {
    struct CaptureConfig {
        std::string devicePath = "/dev/video0";
        std::string inputFormat = "v4l2";
        int width = 0;
        int height = 0;
        int framerate = 30;
        int bufferSize = 4;
    };
}