#include "camera_session.hpp"
#include "../../../video-core/include/common/types.hpp"
#include <iostream>

bool CameraSession::start(const std::string &whipUrl,
                          const std::string &streamKey) {
    if (isRunning_) {
        return false; // Already running
    }

    videoCore::capture::CaptureConfig captureConfig;
    #ifdef _WIN32
        captureConfig.devicePath = "video=0";
        captureConfig.inputFormat = "dshow";
    #else
        captureConfig.devicePath = "/dev/video0";
        captureConfig.inputFormat = "v4l2";
        captureConfig.pixelFormat = "mjpeg";
    #endif
    captureConfig.width = 1280;
    captureConfig.height = 720;
    captureConfig.framerate = 30;

    videoCore::encode::EncoderConfig encoderConfig;
    encoderConfig.width = captureConfig.width;
    encoderConfig.height = captureConfig.height;
    encoderConfig.framerate = captureConfig.framerate;
    encoderConfig.bitrate = 4000000;
    encoderConfig.gopSize = 30; // One keyframe per second at 30fps
    encoderConfig.preset = videoCore::encode::EncoderConfig::Preset::UltraFast;

    auto startResult = pipeline_.initialize(captureConfig, encoderConfig);
    if (startResult != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Failed to initialize video pipeline: "
                  << videoCore::resultToString(startResult)
                  << " (device=" << captureConfig.devicePath
                  << ", format=" << captureConfig.inputFormat
                  << ", pixel_format=" << captureConfig.pixelFormat
                  << ", " << captureConfig.width << "x" << captureConfig.height
                  << "@" << captureConfig.framerate << "fps)\n";
        return false; // Failed to initialize pipeline
    }
    
    auto whipPublisherResult = whipPublisher_.initialize(
        whipUrl, streamKey, encoderConfig.framerate,
        [](const std::string &err) {
            std::cerr << "[CameraSession] WHIPPublisher error: " << err << "\n";
        });
    if (whipPublisherResult != networking::Result::Success) {
        std::cerr << "[CameraSession] Failed to initialize WHIP publisher"
                     " (url=" << whipUrl << ")\n";
        return false;
    }

     whipPublisher_.setKeyframeRequestCallback([this]() {
        pipeline_.requestKeyframe();
    });

    // The WHIP publisher is started first to set running_ to true 
    // before producing frames to avoid dropping frames during
    // publisher startup.
    auto publisherResult = whipPublisher_.start();
    if (publisherResult != networking::Result::Success) {
        std::cerr << "[CameraSession] Failed to start WHIP publisher";
        pipeline_.stop();
        return false;
    }

    auto pipelineResult =
        pipeline_.start([this](std::unique_ptr<videoCore::Packet> pkt) {
            // Notify upstream listeners (e.g. CameraSessionController) that a
            // packet was just produced by the encoder, so they can drive
            // capture-rate-locked work — like sending the latency DC packet —
            // off the transmit rate instead of the capture rate.  Done before
            // moving pkt into pushPacket so we still have access to its pts.
            if (packetEncodedCb_) {
                packetEncodedCb_(pkt->pts);
            }
            whipPublisher_.pushPacket(std::move(pkt));
        });

    if (pipelineResult != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Failed to start video pipeline: "
                  << videoCore::resultToString(pipelineResult) << "\n";
        whipPublisher_.stop();
        return false;
    }

    isRunning_ = true;
    return true;
}

void CameraSession::setPreviewCallback(std::function<void(const videoCore::Frame &)> cb) {
    pipeline_.setPreviewCallback(std::move(cb));
}

void CameraSession::setPacketEncodedCallback(std::function<void(int64_t)> cb) {
    packetEncodedCb_ = std::move(cb);
}

void CameraSession::stop() {
    if (!isRunning_) {
        return; // Not running
    }
    const auto stop_result = pipeline_.stop();
    if (stop_result != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Pipeline stop returned an error: "
                  << videoCore::resultToString(stop_result) << "\n";
    }
    whipPublisher_.stop();
    isRunning_ = false;
}