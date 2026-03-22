#pragma once
#include "encoder.hpp"
#include "encoder_config.hpp"

struct AVCodecContext;

namespace videoCore::encode {
class SoftwareEncoder : public Encoder {
public:
    SoftwareEncoder() = default;
    ~SoftwareEncoder() override;
    SoftwareEncoder(const SoftwareEncoder &) = delete;
    SoftwareEncoder &operator=(const SoftwareEncoder &) = delete;
    SoftwareEncoder(const SoftwareEncoder &&) = delete;
    SoftwareEncoder &operator=(const SoftwareEncoder &&) = delete;

    [[nodiscard]] Result initialize(
        const EncoderConfig &config,
        std::function<void(std::unique_ptr<Packet>)> packetCallback) override;
    [[nodiscard]] Result encodeFrame(AVFrame *frame) override;
    [[nodiscard]] Result stop() override;

private:
    AVCodecContext *codecCtx_ = nullptr;
};
} // namespace videoCore::encode