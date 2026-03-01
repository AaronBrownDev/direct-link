#include "../../../include/common/types.hpp"
#include "gtest/gtest.h"
#include <type_traits>

using namespace videoCore;

TEST(ResultToString, SuccessReturnsCorrectString) {
    EXPECT_EQ(resultToString(Result::Success), "Success");
}

// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
TEST(ResultToString, UnknownValueReturnsUnknownError) {
    EXPECT_EQ(resultToString(static_cast<Result>(7)), "Unknown error");
}
// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)

TEST(Frame, DefaultConstructsCorrectly) {
    videoCore::Frame f;
    EXPECT_EQ(f.pts, 0);
    EXPECT_EQ(f.width, 0);
    EXPECT_EQ(f.frame, nullptr);
}

TEST(Frame, IsMoveOnly) {
    EXPECT_FALSE(std::is_copy_constructible_v<videoCore::Frame>);
    EXPECT_TRUE(std::is_move_constructible_v<videoCore::Frame>);
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
    EXPECT_FALSE(std::is_copy_constructible_v<videoCore::Packet>);
    EXPECT_TRUE(std::is_move_constructible_v<videoCore::Packet>);
}