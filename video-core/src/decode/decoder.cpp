#include "../../include/decode/decoder.hpp"
#include "../../include/decode/nvdec_decoder.hpp"
#include "../../include/decode/software_decoder.hpp"
#include <libavutil/mathematics.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace videoCore::decode {
Result
Decoder::initialize(std::function<void(std::unique_ptr<Frame>)> frameCallback) {
    frameCallback_ = std::move(frameCallback);
    return Result::Success;
}

int64_t Decoder::rescaleToNs(int64_t value, AVRational src_tb) {
    if (value == AV_NOPTS_VALUE) {
        return 0;
    }
    constexpr AVRational ns_tb = {.num = 1, .den = 1000000000};
    return av_rescale_q(value, src_tb, ns_tb);
}

std::unique_ptr<Decoder> createDecoder() {
    if (avcodec_find_decoder_by_name("h264_cuvid") != nullptr) {
        AVBufferRef *hw_ctx = nullptr;
        if (av_hwdevice_ctx_create(&hw_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr,
                                   nullptr, 0) == 0) {
            av_buffer_unref(&hw_ctx);
            return std::make_unique<NvdecDecoder>();
        }
    }
    return std::make_unique<SoftwareDecoder>();
}

} // namespace videoCore::decode