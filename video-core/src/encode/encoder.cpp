#include "../../include/encode/encoder.hpp"
#include "../../include/encode/encoder_config.hpp"
#include "../../include/encode/nvenc_encoder.hpp"
#include "../../include/encode/software_encoder.hpp"
#include "../../include/encode/vaapi_encoder.hpp"

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

std::unique_ptr<Encoder> createEncoder(const EncoderConfig &config,
                                       bool allowHardware) {
    EncoderConfig resolved = config;

    // Default policy when caller asks for Software: pick the best hardware
    // encoder compiled into FFmpeg if one is available, falling back to
    // Software if none are.  Probe order:
    //   1. NVENC  — fastest on NVIDIA, lowest latency profile (~1 ms/frame).
    //   2. VAAPI — AMD VCN / Intel Quick Sync.  Comparable latency to NVENC.
    //   3. Software (libx264) — fallback that always exists in libavcodec-extra.
    //
    // The actual avcodec_open2 still happens in each Encoder::initialize, so
    // the codec being "found" here is necessary but not sufficient — we may
    // still fall back at runtime (e.g. NVENC compiled in but libcuda.so.1
    // not loadable).  VideoPipeline::start retries with allowHardware=false
    // when initialize fails, so the worst case is one wasted open attempt.
    if (resolved.type == EncoderConfig::Type::Software && allowHardware) {
        if (avcodec_find_encoder_by_name("h264_nvenc") != nullptr) {
            resolved.type = EncoderConfig::Type::NVENC;
        }
        else if (avcodec_find_encoder_by_name("h264_vaapi") != nullptr) {
            resolved.type = EncoderConfig::Type::VAAPI;
        }
    }
    else if (resolved.type == EncoderConfig::Type::NVENC) {
        if (!allowHardware ||
            avcodec_find_encoder_by_name("h264_nvenc") == nullptr) {
            // NVENC unavailable — try VAAPI before falling all the way back
            // to software so AMD/Intel hosts still get GPU encoding.
            if (allowHardware &&
                avcodec_find_encoder_by_name("h264_vaapi") != nullptr) {
                resolved.type = EncoderConfig::Type::VAAPI;
            }
            else {
                resolved.type = EncoderConfig::Type::Software;
            }
        }
    }
    else if (resolved.type == EncoderConfig::Type::VAAPI) {
        if (!allowHardware ||
            avcodec_find_encoder_by_name("h264_vaapi") == nullptr) {
            resolved.type = EncoderConfig::Type::Software;
        }
    }

    switch (resolved.type) {
    case EncoderConfig::Type::Software:
        std::cerr << "[Encoder] selected: Software (libx264)\n";
        return std::make_unique<SoftwareEncoder>();
    case EncoderConfig::Type::NVENC:
        std::cerr << "[Encoder] selected: Hardware (h264_nvenc)\n";
        return std::make_unique<NVENCEncoder>();
    case EncoderConfig::Type::VAAPI:
        std::cerr << "[Encoder] selected: Hardware (h264_vaapi)\n";
        return std::make_unique<VAAPIEncoder>();
    default:
        return nullptr;
    }
}

} // namespace videoCore::encode