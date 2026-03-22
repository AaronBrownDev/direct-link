#pragma once
#include "../common/types.hpp"
#include "decoder.hpp"
#include <functional>

struct AVCodecContext;
struct AVFrame;
struct AVBufferRef;

namespace videoCore::decode {
class NvdecDecoder : public Decoder {
public:
    NvdecDecoder() = default;
    ~NvdecDecoder() override;
    NvdecDecoder(const NvdecDecoder &) = delete;
    NvdecDecoder &operator=(const NvdecDecoder &) = delete;
    NvdecDecoder(NvdecDecoder &&) = delete;
    NvdecDecoder &operator=(NvdecDecoder &&) = delete;

    [[nodiscard]] Result initialize(
        std::function<void(std::unique_ptr<Frame>)> frameCallback) override;
    [[nodiscard]] void decodePacket(std::unique_ptr<Packet> packet) override;
    [[nodiscard]] void stop() override;

private:
    AVCodecContext *codecCtx_{nullptr};
    AVBufferRef *hwDeviceCtx_{nullptr};
    AVFrame *frame_{nullptr};
    AVFrame *cpuFrame_{nullptr};
    bool initialized_{false};
};
} // namespace videoCore::decode