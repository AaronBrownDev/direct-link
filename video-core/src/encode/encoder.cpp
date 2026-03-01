#include "../../include/encode/encoder.hpp"
#include "../../include/encode/encoder_config.hpp"
#include "../../include/encode/nvenc_encoder.hpp"
#include "../../include/encode/software_encoder.hpp"

namespace videoCore::encode {

Result Encoder::initialize(
    const EncoderConfig &config,
    std::function<void(std::unique_ptr<Packet>)> packetCallback) {
    config_ = config;
    encodedPacketCallback_ = std::move(packetCallback);
    return Result::Success;
}

std::unique_ptr<Encoder> createEncoder(const EncoderConfig &config) {
    switch (config.type) {
    case EncoderConfig::Type::Software:
        return std::make_unique<SoftwareEncoder>();
    case EncoderConfig::Type::Hardware:
        return std::make_unique<NVENCEncoder>();
    default:
        return nullptr;
    }
}

} // namespace videoCore::encode