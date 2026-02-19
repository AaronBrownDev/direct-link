#include "gtest/gtest.h"
#include "../../include/capture/capture_config.hpp"
#include "../../include/capture/camera_capture.hpp"

using namespace videoCore::capture;

TEST(CameraCapture, DefaultState) {
    CameraCapture capture;
    EXPECT_FALSE(capture.isRunning());
    EXPECT_EQ(capture.getWidth(), 0);
    EXPECT_EQ(capture.getHeight(), 0);
    EXPECT_EQ(capture.getFramerate(), 0);
}

TEST(CameraCapture, StartWithoutInitializeFails) {
    CameraCapture capture;
    auto result = capture.start([](std::unique_ptr<videoCore::Frame>) {});
    EXPECT_EQ(result, videoCore::Result::ErrorInitFailed);
}

TEST(CameraCapture, StopWhenNotRunningSucceeds) {
    CameraCapture capture;
    EXPECT_EQ(capture.stop(), videoCore::Result::Success);
}

TEST(CameraCapture, IsMoveOnly) {
    EXPECT_FALSE(std::is_copy_constructible_v<CameraCapture>);
    EXPECT_FALSE(std::is_move_constructible_v<CameraCapture>);
}