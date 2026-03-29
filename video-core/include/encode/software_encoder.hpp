#pragma once
#include "encoder.hpp"
#include "encoder_config.hpp"
#include <atomic>

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
    void stop() override;
    void requestKeyframe() noexcept override;

private:
    AVCodecContext *codecCtx_ = nullptr;
    std::atomic<bool> forceKeyframe_ = false;
};
} // namespace videoCore::encode