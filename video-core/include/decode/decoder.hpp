#pragma once
#include "../common/types.hpp"
#include <functional>

namespace videoCore::decode {

class Decoder {
public:
    Decoder() = default;
    virtual ~Decoder() = default;
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    Decoder(Decoder &&) = delete;
    Decoder &operator=(Decoder &&) = delete;
    virtual Result
    initialize(std::function<void(std::unique_ptr<Frame>)> frameCallback) = 0;
    virtual void decodePacket(std::unique_ptr<Packet> packet) = 0;
    virtual void stop() = 0;

protected:
    static int64_t rescaleToNs(int64_t value, AVRational src_tb);
    std::function<void(std::unique_ptr<Frame>)> frameCallback_;
};
std::unique_ptr<Decoder> createDecoder();
} // namespace videoCore::decode