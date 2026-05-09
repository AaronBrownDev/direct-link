#include "../../include/encode/vaapi_encoder.hpp"
#include "../../include/common/types.hpp"
#include "../../include/encode/encoder_config.hpp"

#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace videoCore::encode {

VAAPIEncoder::~VAAPIEncoder() {
    if (running_) {
        VAAPIEncoder::stop();
    }
    if (codecCtx_ != nullptr) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (hwFramesCtx_ != nullptr) {
        av_buffer_unref(&hwFramesCtx_);
    }
    if (hwDeviceCtx_ != nullptr) {
        av_buffer_unref(&hwDeviceCtx_);
    }
}

Result VAAPIEncoder::initialize(
    const EncoderConfig &config,
    std::function<void(std::unique_ptr<Packet>)> packetCallback) {

    auto res = Encoder::initialize(config, std::move(packetCallback));
    if (res != Result::Success) {
        return res;
    }

    const AVCodec *encoder = avcodec_find_encoder_by_name("h264_vaapi");
    if (encoder == nullptr) {
        return Result::ErrorDeviceNotFound;
    }

    // Open the default VAAPI device.  Passing NULL lets libva pick the system
    // default — typically /dev/dri/renderD128 on Linux.  This is what FFmpeg's
    // own h264_vaapi example uses; explicit DRI node selection is only needed
    // on multi-GPU rigs where the wrong adapter would be picked otherwise.
    int err = av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_VAAPI,
                                     nullptr, nullptr, 0);
    if (err < 0) {
        char buf[256] = {};
        av_strerror(err, buf, sizeof(buf));
        std::cerr << "[VAAPIEncoder] av_hwdevice_ctx_create failed: " << buf
                  << "\n";
        return Result::ErrorDeviceNotFound;
    }

    // Allocate a hardware frames pool.  VAAPI surfaces hold NV12 internally,
    // so the swframe sw_format is NV12 and the hw_pix_fmt is VAAPI.  The
    // initial pool size of 20 is enough headroom for ~1/3 second of latency
    // at 60 fps even if the encoder briefly stalls.
    hwFramesCtx_ = av_hwframe_ctx_alloc(hwDeviceCtx_);
    if (hwFramesCtx_ == nullptr) {
        std::cerr << "[VAAPIEncoder] av_hwframe_ctx_alloc returned NULL\n";
        return Result::ErrorInitFailed;
    }
    auto *frames =
        reinterpret_cast<AVHWFramesContext *>(hwFramesCtx_->data);
    frames->format = AV_PIX_FMT_VAAPI;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = config_.width;
    frames->height = config_.height;
    frames->initial_pool_size = 20;
    err = av_hwframe_ctx_init(hwFramesCtx_);
    if (err < 0) {
        char buf[256] = {};
        av_strerror(err, buf, sizeof(buf));
        std::cerr << "[VAAPIEncoder] av_hwframe_ctx_init failed: " << buf
                  << "\n";
        return Result::ErrorInitFailed;
    }

    codecCtx_ = avcodec_alloc_context3(encoder);
    if (codecCtx_ == nullptr) {
        return Result::ErrorInitFailed;
    }

    codecCtx_->width = config_.width;
    codecCtx_->height = config_.height;
    codecCtx_->bit_rate = config_.bitrate;
    codecCtx_->time_base = AVRational{.num = 1, .den = config_.framerate};
    codecCtx_->framerate = AVRational{.num = config_.framerate, .den = 1};
    codecCtx_->gop_size = config_.gopSize;
    codecCtx_->max_b_frames = 0;            // No B-frames for low latency
    codecCtx_->pix_fmt = AV_PIX_FMT_VAAPI;  // hardware-resident frames
    codecCtx_->hw_frames_ctx = av_buffer_ref(hwFramesCtx_);

    AVDictionary *options = nullptr;
    // VAAPI exposes "rc_mode" (rate control) and "quality" knobs; CBR with a
    // low-latency preset gives WebRTC-friendly bursty output.  "low_power"
    // chooses the LP encoder pipeline on Intel/AMD where available — uses
    // less GPU and produces smaller, more uniform frames.
    av_dict_set(&options, "rc_mode", "CBR", 0);
    av_dict_set(&options, "quality", "0", 0);
    av_dict_set(&options, "low_power", "1", 0);

    if (avcodec_open2(codecCtx_, encoder, &options) < 0) {
        // Some drivers (older AMD VCN) reject low_power; retry without it.
        av_dict_free(&options);
        options = nullptr;
        av_dict_set(&options, "rc_mode", "CBR", 0);
        av_dict_set(&options, "quality", "0", 0);
        if (avcodec_open2(codecCtx_, encoder, &options) < 0) {
            av_dict_free(&options);
            std::cerr << "[VAAPIEncoder] avcodec_open2 failed\n";
            return Result::ErrorInitFailed;
        }
    }
    av_dict_free(&options);

    running_ = true;
    return Result::Success;
}

Result VAAPIEncoder::encodeFrame(AVFrame *frame) {
    if (!running_) {
        return Result::ErrorEncodeFailed;
    }

    // The pipeline hands us YUV420P / I420 from CameraCapture's appsink.
    // VAAPI surfaces are NV12, so we first convert sw → NV12 (libswscale),
    // then upload the NV12 sw frame onto a VAAPI hwframe.
    AVFrame *nv12 = av_frame_alloc();
    if (nv12 == nullptr) {
        return Result::ErrorEncodeFailed;
    }
    nv12->width = codecCtx_->width;
    nv12->height = codecCtx_->height;
    nv12->format = AV_PIX_FMT_NV12;
    if (av_frame_get_buffer(nv12, 32) < 0) {
        av_frame_free(&nv12);
        return Result::ErrorEncodeFailed;
    }

    SwsContext *sws = sws_getContext(
        frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format),
        codecCtx_->width, codecCtx_->height, AV_PIX_FMT_NV12,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        av_frame_free(&nv12);
        return Result::ErrorEncodeFailed;
    }
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
              nv12->data, nv12->linesize);
    sws_freeContext(sws);

    AVFrame *hw_frame = av_frame_alloc();
    if (hw_frame == nullptr) {
        av_frame_free(&nv12);
        return Result::ErrorEncodeFailed;
    }
    if (av_hwframe_get_buffer(hwFramesCtx_, hw_frame, 0) < 0) {
        av_frame_free(&hw_frame);
        av_frame_free(&nv12);
        return Result::ErrorEncodeFailed;
    }
    if (av_hwframe_transfer_data(hw_frame, nv12, 0) < 0) {
        av_frame_free(&hw_frame);
        av_frame_free(&nv12);
        return Result::ErrorEncodeFailed;
    }

    hw_frame->pts = av_rescale_q(
        frame->pts, AVRational{.num = 1, .den = 1000000000},
        codecCtx_->time_base);

    int ret = avcodec_send_frame(codecCtx_, hw_frame);
    av_frame_free(&hw_frame);
    av_frame_free(&nv12);
    if (ret < 0) {
        return Result::ErrorEncodeFailed;
    }

    AVPacket *pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        auto wrapped = std::make_unique<Packet>();
        wrapped->packet.reset(av_packet_clone(pkt));

        AVRational tb = codecCtx_->time_base;
        wrapped->pts = Encoder::rescaleToNs(pkt->pts, tb);
        wrapped->dts = Encoder::rescaleToNs(pkt->dts, tb);
        wrapped->size = pkt->size;
        wrapped->isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

        if (encodedPacketCallback_) {
            encodedPacketCallback_(std::move(wrapped));
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return Result::Success;
}

void VAAPIEncoder::stop() {
    if (codecCtx_ == nullptr) {
        return;
    }
    avcodec_send_frame(codecCtx_, nullptr);  // flush

    AVPacket *pkt = av_packet_alloc();
    while (avcodec_receive_packet(codecCtx_, pkt) == 0) {
        auto wrapped = std::make_unique<Packet>();
        wrapped->packet.reset(av_packet_clone(pkt));
        AVRational tb = codecCtx_->time_base;
        wrapped->pts = Encoder::rescaleToNs(pkt->pts, tb);
        wrapped->dts = Encoder::rescaleToNs(pkt->dts, tb);
        wrapped->size = pkt->size;
        wrapped->isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        if (encodedPacketCallback_) {
            encodedPacketCallback_(std::move(wrapped));
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
