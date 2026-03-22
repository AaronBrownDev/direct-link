#pragma once
#include "../common/types.hpp"
#include "decoder.hpp"
#include <functional>

struct AVCodecContext;
struct AVFrame;
struct AVBufferRef;

namespace videoCore::decode {
class SoftwareDecoder : public Decoder {
public:
    SoftwareDecoder() = default;
    ~SoftwareDecoder() override;
    SoftwareDecoder(const SoftwareDecoder &) = delete;
    SoftwareDecoder &operator=(const SoftwareDecoder &) = delete;
    SoftwareDecoder(SoftwareDecoder &&) = delete;
    SoftwareDecoder &operator=(SoftwareDecoder &&) = delete;

    [[nodiscard]] Result initialize(
        std::function<void(std::unique_ptr<Frame>)> frameCallback) override;
    void decodePacket(std::unique_ptr<Packet> packet) override;
    void stop() override;

private:
    AVCodecContext *codecCtx_{nullptr};
    AVFrame *frame_{nullptr};
    bool initialized_{false};
};
} // namespace videoCore::decode