#pragma once
#include "encoder.hpp"
#include "encoder_config.hpp"

struct AVCodecContext;

namespace videoCore::encode {
class NVENCEncoder : public Encoder {
public:
    NVENCEncoder() = default;
    ~NVENCEncoder() override;
    NVENCEncoder(const NVENCEncoder &) = delete;
    NVENCEncoder &operator=(const NVENCEncoder &) = delete;
    NVENCEncoder(const NVENCEncoder &&) = delete;
    NVENCEncoder &operator=(const NVENCEncoder &&) = delete;

    [[nodiscard]] Result initialize(
        const EncoderConfig &config,
        std::function<void(std::unique_ptr<Packet>)> packetCallback) override;
    [[nodiscard]] Result encodeFrame(AVFrame *frame) override;
    [[nodiscard]] Result stop() override;
private:
    AVCodecContext *codecCtx_ = nullptr;
};
} // namespace videoCore::encode