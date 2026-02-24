#pragma once
#include "../common/types.hpp"
#include "encoder_config.hpp"
#include <functional>

struct AVFrame;

namespace videoCore::encode {
class Encoder {
public:
    Encoder(const EncoderConfig& config);
    virtual ~Encoder();

    virtual Result initialize(const EncoderConfig& config, 
        std::function<void(std::unique_ptr<Packet>)> packetCallback);
    virtual Result encodeFrame(AVFrame* frame) = 0;
    virtual Result stop() = 0;

    virtual bool isRunning() const { return running_; };

protected:
    EncoderConfig config_;
    std::function<void(std::unique_ptr<Packet>)> encodedPacketCallback_;
    bool running_ = false;
};

std::unique_ptr<Encoder> createEncoder(const EncoderConfig& config);
}