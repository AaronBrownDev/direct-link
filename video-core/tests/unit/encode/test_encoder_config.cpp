#include "../../../include/encode/encoder_config.hpp"
#include "gtest/gtest.h"

using namespace videoCore::encode;

TEST(EncoderConfig, DefaultValues) {
    EncoderConfig config;
    EXPECT_EQ(config.width, 0);
    EXPECT_EQ(config.height, 0);
    EXPECT_EQ(config.framerate, 30);
    EXPECT_EQ(config.gopSize, 60);
    EXPECT_EQ(config.preset, EncoderConfig::Preset::UltraFast);
    EXPECT_EQ(config.type, EncoderConfig::Type::Software);
}