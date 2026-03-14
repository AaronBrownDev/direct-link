#include "../../include/decode/nvdec_decoder.hpp"
#include "../../include/common/types.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace videoCore::decode {

NvdecDecoder::~NvdecDecoder() {
    if (initialized_) {
        avcodec_send_packet(codecCtx_, nullptr);
        while (avcodec_receive_frame(codecCtx_, frame_) >= 0) {
            av_frame_unref(frame_);
        }
        initialized_ = false;
    }
    if (frame_ != nullptr) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (cpuFrame_ != nullptr) {
        av_frame_free(&cpuFrame_);
        cpuFrame_ = nullptr;
    }
    if (codecCtx_ != nullptr) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (hwDeviceCtx_ != nullptr) {
        av_buffer_unref(&hwDeviceCtx_);
        hwDeviceCtx_ = nullptr;
    }
}

Result NvdecDecoder::initialize(
    std::function<void(std::unique_ptr<Frame>)> frameCallback) {
    Decoder::initialize(std::move(frameCallback));

    if (av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_CUDA, nullptr,
                               nullptr, 0) < 0) {
        return Result::ErrorDeviceNotFound;
    }

    const AVCodec *decoder = avcodec_find_decoder_by_name("h264_cuvid");
    if (decoder == nullptr) {
        return Result::ErrorDeviceNotFound;
    }

    codecCtx_ = avcodec_alloc_context3(decoder);
    if (codecCtx_ == nullptr) {
        return Result::ErrorInitFailed;
    }

    codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);

    if (avcodec_open2(codecCtx_, decoder, nullptr) < 0) {
        return Result::ErrorInitFailed;
    }

    frame_ = av_frame_alloc();
    cpuFrame_ = av_frame_alloc();
    if (frame_ == nullptr || cpuFrame_ == nullptr) {
        return Result::ErrorInitFailed;
    }

    initialized_ = true;
    return Result::Success;
}

void NvdecDecoder::decodePacket(std::unique_ptr<Packet> packet) {
    if (!initialized_) {
        return;
    }

    if (avcodec_send_packet(codecCtx_, packet->packet.get()) < 0) {
        return;
    }

    while (avcodec_receive_frame(codecCtx_, frame_) == 0) {
        if (av_hwframe_transfer_data(cpuFrame_, frame_, 0) < 0) {
            av_frame_unref(frame_);
            continue;
        }

        auto wrapped_frame = std::make_unique<Frame>();
        wrapped_frame->frame.reset(av_frame_clone(cpuFrame_));

        AVRational decoder_tb = codecCtx_->time_base;
        wrapped_frame->pts = Decoder::rescaleToNs(frame_->pts, decoder_tb);
        wrapped_frame->width = cpuFrame_->width;
        wrapped_frame->height = cpuFrame_->height;
        wrapped_frame->format = static_cast<AVPixelFormat>(cpuFrame_->format);

        if (frameCallback_) {
            frameCallback_(std::move(wrapped_frame));
        }

        av_frame_unref(frame_);
        av_frame_unref(cpuFrame_);
    }
}

void NvdecDecoder::stop() {
    if (!initialized_) {
        return;
    }

    avcodec_send_packet(codecCtx_, nullptr);

    while (avcodec_receive_frame(codecCtx_, frame_) >= 0) {
        av_frame_unref(frame_);
    }

    avcodec_free_context(&codecCtx_);
    codecCtx_ = nullptr;
    initialized_ = false;
}

} // namespace videoCore::decode