#include "../../include/capture/camera_capture.hpp"
#include "../../include/capture/capture_config.hpp"
#include <functional>
#include <thread>
#include <chrono>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace videoCore::capture {
CameraCapture::~CameraCapture() {
    stop();

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }
}

Result CameraCapture::initialize(const CaptureConfig& config) {
    config_ = config;

    auto res = setupDevice();
    if (res != Result::Success) { 
        return res;
    }

    res = setupCodec();
    if (res != Result::Success){ 
        return res;
    }

    return Result::Success;
}

Result CameraCapture::start(std::function<void(std::unique_ptr<Frame>)> frameCallback) {
    if (formatCtx_ == nullptr) {
        return Result::ErrorInitFailed; // Not initialized
    }
    if (isRunning()) {
        return Result::ErrorInitFailed; // Already running
    }

    frameCallback_ = std::move(frameCallback);

    captureThread_ = std::jthread([this](std::stop_token token) {
        captureLoop(token);
    });
    return Result::Success;
}

Result CameraCapture::stop() {
    if (captureThread_.joinable()) {
        captureThread_.request_stop();
        captureThread_.join();
    }
    return Result::Success;
}

int CameraCapture::getWidth() const {
    return codecCtx_ ? codecCtx_->width : 0;
}

int CameraCapture::getHeight() const {
    return codecCtx_ ? codecCtx_->height : 0;
}

int CameraCapture::getFramerate() const {
    return codecCtx_ ? codecCtx_->framerate.num : 0;
}

Result CameraCapture::setupDevice() {
    avdevice_register_all();

    const AVInputFormat* inputFmt = av_find_input_format(config_.inputFormat.c_str());
    if (!inputFmt) {
        return Result::ErrorInvalidParameter;
    }

    formatCtx_ = avformat_alloc_context();
    if (avformat_open_input(&formatCtx_, config_.devicePath.c_str(), 
                            inputFmt, nullptr) < 0) {
        return Result::ErrorDeviceNotFound;
    }

    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        return Result::ErrorInitFailed;
    }

    // Find video stream
    for (unsigned i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx_ = i;
            break;
        }
    }

    // If no video stream found, return error
    if (videoStreamIdx_ == -1) {
        return Result::ErrorDeviceNotFound;
    }

    return Result::Success;
}

Result CameraCapture::setupCodec() {
    if (videoStreamIdx_ == -1) {
        return Result::ErrorInitFailed; // No video stream
    }

    auto* codecpar = formatCtx_->streams[videoStreamIdx_]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        return Result::ErrorInitFailed; // Decoder not found
    }

    codecCtx_ = avcodec_alloc_context3(decoder);
    if (!codecCtx_) {
        return Result::ErrorInitFailed; // Could not allocate codec context
    }

    if (avcodec_parameters_to_context(codecCtx_, codecpar) < 0) {
        return Result::ErrorInitFailed; // Could not copy codec parameters
    }

    if (avcodec_open2(codecCtx_, decoder, nullptr) < 0) {
        return Result::ErrorInitFailed;
    }
    return Result::Success;
}

void CameraCapture::captureLoop(std::stop_token stopToken) {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame   = av_frame_alloc();

    while (!stopToken.stop_requested()) {
        if (av_read_frame(formatCtx_, packet) >= 0) {
            if (packet->stream_index == videoStreamIdx_) {
                if (avcodec_send_packet(codecCtx_, packet) == 0) {
                    if (avcodec_receive_frame(codecCtx_, frame) == 0) {
                        auto wrappedFrame = std::make_unique<Frame>();
                        wrappedFrame->frame.reset(av_frame_clone(frame));
                        wrappedFrame->pts    = frame->pts;
                        wrappedFrame->width  = frame->width;
                        wrappedFrame->height = frame->height;
                        wrappedFrame->format = static_cast<AVPixelFormat>(frame->format);

                        if (frameCallback_) {
                            frameCallback_(std::move(wrappedFrame));
                        }
                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(packet);
        } 
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}