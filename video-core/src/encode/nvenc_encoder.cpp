#include "../../include/encode/nvenc_encoder.hpp"
#include "../../include/common/types.hpp"
#include "../../include/encode/encoder_config.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace videoCore::encode {
NVENCEncoder::~NVENCEncoder() {
    if (running_) {
        NVENCEncoder::stop();
    }

    if (codecCtx_ != nullptr) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
}

Result NVENCEncoder::initialize(
    const EncoderConfig &config,
    std::function<void(std::unique_ptr<Packet>)> packetCallback) {

    auto res = Encoder::initialize(config, std::move(packetCallback));
    if (res != Result::Success) {
        return res;
    }

    // Initialize NVENC encoder context (codecCtx_)
    // Set up codec parameters based on config_
    const AVCodec *encoder = avcodec_find_encoder_by_name("h264_nvenc");
    if (encoder == nullptr) {
        return Result::ErrorDeviceNotFound; // no GPU available or NVENC not
                                            // supported
    }

    codecCtx_ = avcodec_alloc_context3(encoder);
    if (codecCtx_ == nullptr) {
        return Result::ErrorInitFailed; // Failed to allocate context
    }

    // Set codec parameters
    codecCtx_->width = config_.width;
    codecCtx_->height = config_.height;
    codecCtx_->bit_rate = config_.bitrate;
    codecCtx_->time_base = AVRational{1, config_.framerate};
    codecCtx_->framerate = AVRational{config_.framerate, 1};
    codecCtx_->gop_size = config_.gopSize;
    codecCtx_->max_b_frames = 0;          // No B-frames for low latency
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P; // Match camera capture output

    // Set preset options (e.g., ultrafast, fast, medium, slow)
    AVDictionary *options = nullptr;
    switch (config_.preset) {
    case EncoderConfig::Preset::UltraFast:
        av_dict_set(&options, "preset", "p1", 0);
        break;
    case EncoderConfig::Preset::Fast:
        av_dict_set(&options, "preset", "p3", 0);
        break;
    case EncoderConfig::Preset::Medium:
        av_dict_set(&options, "preset", "p5", 0);
        break;
    case EncoderConfig::Preset::Slow:
        av_dict_set(&options, "preset", "p7", 0);
        break;
    }
    // tune — always set for live streaming regardless of preset
    // disables encoder features that add latency (e.g., B-frames, lookahead)
    av_dict_set(&options, "tune", "ull", 0);

    if (avcodec_open2(codecCtx_, encoder, &options) < 0) {
        av_dict_free(&options);
        return Result::ErrorInitFailed;
    }
    av_dict_free(&options);

    running_ = true;
    return Result::Success;
}

Result NVENCEncoder::encodeFrame(AVFrame *frame) {
    if (!running_) {
        return Result::ErrorEncodeFailed; // Not initialized
    }

    // Convert frame to YUV420P with proper stride if needed
    AVFrame *input_frame = frame;
    AVFrame *converted_frame = nullptr;

    if (frame->format != codecCtx_->pix_fmt ||
        frame->width != codecCtx_->width ||
        frame->height != codecCtx_->height ||
        frame->linesize[0] == 0) {
        converted_frame = av_frame_alloc();
        converted_frame->width = codecCtx_->width;
        converted_frame->height = codecCtx_->height;
        converted_frame->format = codecCtx_->pix_fmt;
        av_frame_get_buffer(converted_frame, 32);

        // Convert to YUV420P if needed
        SwsContext *sws_ctx = sws_getContext(
            frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), codecCtx_->width,
            codecCtx_->height, codecCtx_->pix_fmt, SWS_BILINEAR, nullptr,
            nullptr, nullptr);

        // Perform conversion if swsCtx is valid
        if (sws_ctx != nullptr) {
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
                      converted_frame->data, converted_frame->linesize);
            sws_freeContext(sws_ctx);
            converted_frame->pts = frame->pts;
            input_frame = converted_frame;
        }
    }

    // Convert PTS from nanoseconds to encoder timebase
    input_frame->pts =
        av_rescale_q(frame->pts, // already nanoseconds from CameraCapture
                     AVRational{1, 1000000000}, // from
                     codecCtx_->time_base       // to encoder timebase
        );

    // Send frame to encoder
    int ret = avcodec_send_frame(codecCtx_, input_frame);
    if (converted_frame != nullptr) {
        av_frame_free(&converted_frame);
    }
    if (ret < 0) {
        return Result::ErrorEncodeFailed;
    }

    // Receive packets from encoder
    AVPacket *pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        auto wrapped_packet = std::make_unique<Packet>();
        wrapped_packet->packet.reset(av_packet_clone(pkt));

        // Convert PTS/DTS to nanoseconds
        AVRational encoder_tb = codecCtx_->time_base;
        wrapped_packet->pts = Encoder::rescaleToNs(pkt->pts, encoder_tb);
        wrapped_packet->dts = Encoder::rescaleToNs(pkt->dts, encoder_tb);

        wrapped_packet->size = pkt->size;
        wrapped_packet->isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

        if (encodedPacketCallback_) {
            encodedPacketCallback_(std::move(wrapped_packet));
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return Result::Success;
}

void NVENCEncoder::stop() {
    if (codecCtx_ == nullptr) {
        return;
    }

    avcodec_send_frame(codecCtx_, nullptr); // Flush encoder

    // Clean up any remaining packets
    AVPacket *pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        auto wrapped_packet = std::make_unique<Packet>();
        wrapped_packet->packet.reset(av_packet_clone(pkt));

        // Convert PTS/DTS to nanoseconds
        AVRational encoder_tb = codecCtx_->time_base;
        wrapped_packet->pts = Encoder::rescaleToNs(pkt->pts, encoder_tb);
        wrapped_packet->dts = Encoder::rescaleToNs(pkt->dts, encoder_tb);

        wrapped_packet->size = pkt->size;
        wrapped_packet->isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        if (encodedPacketCallback_) {
            encodedPacketCallback_(std::move(wrapped_packet));
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    if (codecCtx_ != nullptr) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    running_ = false;
}

} // namespace videoCore::encode