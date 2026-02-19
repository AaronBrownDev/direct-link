#pragma once

#include "../../include/capture/camera_capture.hpp"
#include "../../include/capture/capture_config.hpp"
#include <functional>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace videoCore::capture {
CameraCapture::~CameraCapture() {
    stop();
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
    if (isRunning()) {
        return Result::ErrorInitFailed; // Already running
    }

    frameCallback_ = std::move(frameCallback);
    captureThread_ = std::jthread(&CameraCapture::captureLoop, this);
    return Result::Success;
}

Result CameraCapture::stop() {
    if (captureThread_.joinable()) {
        captureThread_.request_stop();
        captureThread_.join();
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }

    return Result::Success;
}

int CameraCapture::getWidth() const {
    return config_.width;
}

int CameraCapture::getHeight() const {
    return config_.height;
}

int CameraCapture::getFramerate() const {
    return config_.framerate;
}
}