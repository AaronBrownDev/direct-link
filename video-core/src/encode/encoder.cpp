#include "../../include/encode/encoder.hpp"
#include "../../include/encode/encoder_config.hpp"
#include "../../include/encode/nvenc_encoder.hpp"
#include "../../include/encode/software_encoder.hpp"

#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
}

namespace videoCore::encode {

Result Encoder::initialize(
    const EncoderConfig &config,
    std::function<void(std::unique_ptr<Packet>)> packetCallback) {
    config_ = config;
    encodedPacketCallback_ = std::move(packetCallback);
    return Result::Success;
}

int64_t Encoder::rescaleToNs(int64_t value, AVRational src_tb) {
    if (value == AV_NOPTS_VALUE) {
        return 0;
    }
    constexpr AVRational ns_tb = {.num = 1, .den = 1000000000};
    return av_rescale_q(value, src_tb, ns_tb);
}

std::unique_ptr<Encoder> createEncoder(const EncoderConfig &config) {
    EncoderConfig resolved = config;

    if (resolved.type == EncoderConfig::Type::Software) {
        // Probe for NVENC — upgrade if available
        if (avcodec_find_encoder_by_name("h264_nvenc") != nullptr) {
            resolved.type = EncoderConfig::Type::Hardware;
        }
    }
    else if (resolved.type == EncoderConfig::Type::Hardware) {
        // Requested hardware but verify it's actually available
        if (avcodec_find_encoder_by_name("h264_nvenc") == nullptr) {
            resolved.type = EncoderConfig::Type::Software;
        }
    }

    switch (resolved.type) {
    case EncoderConfig::Type::Software:
        std::cerr << "[Encoder] selected: Software (libx264)\n";
        return std::make_unique<SoftwareEncoder>();
    case EncoderConfig::Type::Hardware:
        std::cerr << "[Encoder] selected: Hardware (h264_nvenc)\n";
        return std::make_unique<NVENCEncoder>();
    default:
        return nullptr;
    }
}

} // namespace videoCore::encode