#pragma once
#include "../common/types.hpp"
#include "encoder_config.hpp"
#include <functional>

struct AVFrame;

namespace videoCore::encode {
class Encoder {
public:
    Encoder() = default;
    virtual ~Encoder() = default;
    Encoder(const Encoder &) = delete;
    Encoder &operator=(const Encoder &) = delete;
    Encoder(const Encoder &&) = delete;
    Encoder &operator=(const Encoder &&) = delete;

    [[nodiscard]] virtual Result
    initialize(const EncoderConfig &config,
               std::function<void(std::unique_ptr<Packet>)> packetCallback);
    [[nodiscard]] virtual Result encodeFrame(AVFrame *frame) = 0;
    virtual void stop() = 0;
    // Requests an IDR (instantaneous decoder refresh) to allow remote decoder
    // to recover after packet loss
    virtual void requestKeyframe() noexcept {}

    [[nodiscard]] virtual bool isRunning() const { return running_; };

protected:
    EncoderConfig config_;
    std::function<void(std::unique_ptr<Packet>)> encodedPacketCallback_;
    bool running_ = false;
    static int64_t rescaleToNs(int64_t value, AVRational src_tb);
};

std::unique_ptr<Encoder> createEncoder(const EncoderConfig &config);
} // namespace videoCore::encode