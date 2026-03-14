#pragma once
#include "../common/types.hpp"
#include "decoder.hpp"
#include <functional>

struct AVCodecContext;

namespace videoCore::decode {
class NvdecDecoder : public Decoder {
public:
    NvdecDecoder() = default;
    ~NvdecDecoder() override;
    NvdecDecoder(const NvdecDecoder &) = delete;
    NvdecDecoder &operator=(const NvdecDecoder &) = delete;
    NvdecDecoder(const NvdecDecoder &&) = delete;
    NvdecDecoder &operator=(const NvdecDecoder &&) = delete;

    Result initialize(
        std::function<void(std::unique_ptr<Frame>)> frameCallback) override;
    void decodePacket(std::unique_ptr<Packet> packet) override;
    void stop() override;

private:
    AVCodecContext *codecCtx_{nullptr};
    AVBufferRef *hwDeviceCtx_{nullptr};
    AVFrame *frame_{nullptr};
    AVFrame *cpuFrame_{nullptr};
};
} // namespace videoCore::decode