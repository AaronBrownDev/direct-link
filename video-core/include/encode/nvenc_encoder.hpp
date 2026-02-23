#pragma once
#include "encoder_config.hpp"
#include "encoder.hpp"

struct AVCodecContext;

namespace videoCore::encode {
class NVENCEncoder : public Encoder {
public:
    NVENCEncoder() = default;
    ~NVENCEncoder() override;

    Result initialize(const EncoderConfig& config, 
        std::function<void(std::unique_ptr<Packet>)>& packetCallback) override;
    Result encodeFrame(AVFrame* frame) override;
    Result stop() override;

private:
    AVCodecContext* codecCtx_ = nullptr;
};
}