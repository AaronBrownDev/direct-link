#include "../../include/common/types.hpp"
#include "../../include/encode/encoder_config.hpp"
#include "../../include/encode/software_encoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace videoCore::encode {
SoftwareEncoder::~SoftwareEncoder() {
    if (running_) {
        stop();
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
}

Result SoftwareEncoder::initialize(const EncoderConfig& config, 
    std::function<void(std::unique_ptr<Packet>)> packetCallback) {
    
    Encoder::initialize(config, std::move(packetCallback));

    // Initialize x264 encoder context (codecCtx_)
    // Set up codec parameters based on config_
    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) {
        return Result::ErrorInitFailed; // Codec not found
    }

    codecCtx_ = avcodec_alloc_context3(encoder);
    if (!codecCtx_) {
        return Result::ErrorInitFailed; // Failed to allocate context
    }

    // Set codec parameters
    codecCtx_->width = config_.width;
    codecCtx_->height = config_.height;
    codecCtx_->bit_rate = config_.bitrate;
    codecCtx_->time_base = AVRational{1, config_.framerate};
    codecCtx_->framerate = AVRational{config_.framerate, 1};
    codecCtx_->gop_size = config_.gopSize;
    codecCtx_->max_b_frames = 0; // No B-frames for low latency
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P; // Common pixel format
    
    // Set preset options (e.g., ultrafast, fast, medium, slow)
    AVDictionary* options = nullptr;
    switch (config_.preset) {
        case EncoderConfig::Preset::UltraFast:
            av_dict_set(&options, "preset", "ultrafast", 0);
            break;
        case EncoderConfig::Preset::Fast:
            av_dict_set(&options, "preset", "fast", 0);
            break;
        case EncoderConfig::Preset::Medium:
            av_dict_set(&options, "preset", "medium", 0);
            break;
        case EncoderConfig::Preset::Slow:
            av_dict_set(&options, "preset", "slow", 0);
            break;
    }
    // tune — always set for live streaming regardless of preset
    // disables encoder features that add latency (e.g., B-frames, lookahead)
    av_dict_set(&options, "tune", "zerolatency", 0);

    if (avcodec_open2(codecCtx_, encoder, &options) < 0) {
        av_dict_free(&options);
        return Result::ErrorInitFailed; // Failed to open codec
    }
    av_dict_free(&options);

    running_ = true;
    return Result::Success;

}

Result SoftwareEncoder::encodeFrame(AVFrame* frame) {
    if (!running_) {
        return Result::ErrorEncodeFailed; // Not initialized
    }

    // Send frame to encoder
    if (avcodec_send_frame(codecCtx_, frame) < 0) {
        return Result::ErrorEncodeFailed; // Failed to send frame
    }

    // Receive packets from encoder
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        auto wrappedPacket = std::make_unique<Packet>();
        wrappedPacket->packet.reset(av_packet_clone(pkt));
        wrappedPacket->pts = pkt->pts;
        wrappedPacket->dts = pkt->dts;
        wrappedPacket->size = pkt->size;
        wrappedPacket->isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

        if (encodedPacketCallback_) {
            encodedPacketCallback_(std::move(wrappedPacket));
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return Result::Success;

}

Result SoftwareEncoder::stop() {
    if (!codecCtx_) return Result::Success;

    avcodec_send_frame(codecCtx_, nullptr); // Flush encoder

     // Clean up any remaining packets
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        auto wrappedPacket = std::make_unique<Packet>();
        wrappedPacket->packet.reset(av_packet_clone(pkt));
        wrappedPacket->pts       = pkt->pts;
        wrappedPacket->dts       = pkt->dts;
        wrappedPacket->size      = pkt->size;
        wrappedPacket->isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        if (encodedPacketCallback_) {
            encodedPacketCallback_(std::move(wrappedPacket));
        }
        av_packet_unref(pkt);
    
    }
    av_packet_free(&pkt);

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    running_ = false;
    return Result::Success;
}

}