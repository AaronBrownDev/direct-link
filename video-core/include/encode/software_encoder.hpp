#pragma once
#include "encoder_config.hpp"
#include "encoder.hpp"

struct AVCodecContext;

namespace videoCore::encode {
class SoftwareEncoder : public Encoder {
public:
    SoftwareEncoder() = default;
    ~SoftwareEncoder() override;

    Result initialize(const EncoderConfig& config, 
        std::function<void(std::unique_ptr<Packet>)>& packetCallback) override;
    Result encodeFrame(AVFrame* frame) override;
    Result stop() override;

private:
    // Internal state for x264 encoder
    AVCodecContext* codecCtx_ = nullptr;
};
}