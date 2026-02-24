#include "gtest/gtest.h"
#include "../../../include/common/types.hpp"
#include <type_traits>

using namespace videoCore;

TEST(ResultToString, SuccessReturnsCorrectString) {
    EXPECT_EQ(resultToString(Result::Success), "Success");
}

TEST(ResultToString, UnknownValueReturnsUnknownError) {
    EXPECT_EQ(resultToString(static_cast<Result>(999)), "Unknown error");
}

TEST(Frame, DefaultConstructsCorrectly) {
    videoCore::Frame f;
    EXPECT_EQ(f.pts, 0);
    EXPECT_EQ(f.width, 0);
    EXPECT_EQ(f.frame, nullptr);
}

TEST(Frame, IsMoveOnly) {
    EXPECT_FALSE(std::is_copy_constructible<videoCore::Frame>::value);
    EXPECT_TRUE(std::is_move_constructible<videoCore::Frame>::value);
}

TEST(Packet, DefaultConstructsCorrectly) {
    videoCore::Packet f;
    EXPECT_EQ(f.pts, 0);
    EXPECT_EQ(f.dts, 0);
    EXPECT_EQ(f.size, 0);
    EXPECT_EQ(f.isKeyframe, false);
    EXPECT_EQ(f.packet, nullptr);
}

TEST(Packet, IsMoveOnly) {
    EXPECT_FALSE(std::is_copy_constructible<videoCore::Packet>::value);
    EXPECT_TRUE(std::is_move_constructible<videoCore::Packet>::value);
}