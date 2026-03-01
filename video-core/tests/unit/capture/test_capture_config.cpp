#include "../../../include/capture/capture_config.hpp"
#include "gtest/gtest.h"

using namespace videoCore::capture;

TEST(CaptureConfig, DefaultValues) {
    CaptureConfig config;
    EXPECT_EQ(config.devicePath, "/dev/video0");
    EXPECT_EQ(config.inputFormat, "v4l2");
    EXPECT_EQ(config.width, 0);
    EXPECT_EQ(config.height, 0);
    EXPECT_EQ(config.framerate, 30);
    EXPECT_EQ(config.bufferSize, 4);
}