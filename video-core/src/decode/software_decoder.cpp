#include "../../include/decode/software_decoder.hpp"
#include "../../include/common/types.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
}

namespace videoCore::decode {

SoftwareDecoder::~SoftwareDecoder() {
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
    if (codecCtx_ != nullptr) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
}

Result SoftwareDecoder::initialize(
    std::function<void(std::unique_ptr<Frame>)> frameCallback) {
    auto res = Decoder::initialize(std::move(frameCallback));
    if (res != Result::Success) {
        return res;
    }

    const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (decoder == nullptr) {
        return Result::ErrorInitFailed;
    }

    codecCtx_ = avcodec_alloc_context3(decoder);
    if (codecCtx_ == nullptr) {
        return Result::ErrorInitFailed;
    }

    // Single-threaded to minimize per-frame decode latency; multi-threaded
    // frame parallelism requires lookahead buffering which adds unacceptable
    // latency for live streaming
    codecCtx_->thread_count = 1;

    if (avcodec_open2(codecCtx_, decoder, nullptr) < 0) {
        return Result::ErrorInitFailed;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        return Result::ErrorInitFailed;
    }

    initialized_ = true;
    return Result::Success;
}

void SoftwareDecoder::decodePacket(std::unique_ptr<Packet> packet) {
    if (!initialized_) {
        return;
    }

    if (avcodec_send_packet(codecCtx_, packet->packet.get()) < 0) {
        return;
    }

    while (avcodec_receive_frame(codecCtx_, frame_) == 0) {
        auto wrapped_frame = std::make_unique<Frame>();
        wrapped_frame->frame.reset(av_frame_clone(frame_));

        AVRational decoder_tb = codecCtx_->time_base;
        wrapped_frame->pts = Decoder::rescaleToNs(frame_->pts, decoder_tb);
        wrapped_frame->width = frame_->width;
        wrapped_frame->height = frame_->height;
        wrapped_frame->format = static_cast<AVPixelFormat>(frame_->format);

        if (frameCallback_) {
            frameCallback_(std::move(wrapped_frame));
        }

        av_frame_unref(frame_);
    }
}

void SoftwareDecoder::stop() {
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