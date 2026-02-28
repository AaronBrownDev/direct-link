#include "gtest/gtest.h"
#include "../../../include/pipeline/video_pipeline.hpp"

using namespace videoCore::pipeline;

TEST(VideoPipeline, DefaultState) {
    VideoPipeline pipeline;
    EXPECT_EQ(pipeline.getFrameCount(), 0);
    EXPECT_EQ(pipeline.getCurrentFramerate(), 0);
    EXPECT_EQ(pipeline.getBitrate(), 0);
    EXPECT_FLOAT_EQ(pipeline.getFPS(), 0.0f);
}

TEST(VideoPipeline, StartWithoutInitializeFails) {
    VideoPipeline pipeline;
    auto result = pipeline.start([](std::unique_ptr<videoCore::Packet>) {});
    EXPECT_EQ(result, videoCore::Result::ErrorInitFailed);
}

TEST(VideoPipeline, StopWhenNotRunningSucceeds) {
    VideoPipeline pipeline;
    EXPECT_EQ(pipeline.stop(), videoCore::Result::Success);
}

TEST(VideoPipeline, IsNonCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<VideoPipeline>);
    EXPECT_FALSE(std::is_copy_assignable_v<VideoPipeline>);
}