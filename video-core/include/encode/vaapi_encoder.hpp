#pragma once
#include "encoder.hpp"
#include "encoder_config.hpp"

struct AVCodecContext;
struct AVBufferRef;

namespace videoCore::encode {

// FFmpeg h264_vaapi wrapper.  Targets AMD (VCN) and Intel (Quick Sync) GPUs
// via libva.  Operates with hardware-resident NV12 frames; sw frames handed
// in to encodeFrame are uploaded to a VAAPI surface pool before encode.
class VAAPIEncoder : public Encoder {
public:
    VAAPIEncoder() = default;
    ~VAAPIEncoder() override;
    VAAPIEncoder(const VAAPIEncoder &) = delete;
    VAAPIEncoder &operator=(const VAAPIEncoder &) = delete;
    VAAPIEncoder(const VAAPIEncoder &&) = delete;
    VAAPIEncoder &operator=(const VAAPIEncoder &&) = delete;

    [[nodiscard]] Result initialize(
        const EncoderConfig &config,
        std::function<void(std::unique_ptr<Packet>)> packetCallback) override;
    [[nodiscard]] Result encodeFrame(AVFrame *frame) override;
    void stop() override;

private:
    AVCodecContext *codecCtx_ = nullptr;
    AVBufferRef *hwDeviceCtx_ = nullptr;
    AVBufferRef *hwFramesCtx_ = nullptr;
};

} // namespace videoCore::encode
