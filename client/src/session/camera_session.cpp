#include "camera_session.hpp"
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
    #endif
    captureConfig.width = 640;
    captureConfig.height = 480;
    captureConfig.framerate = 30;

    videoCore::encode::EncoderConfig encoderConfig;
    encoderConfig.width = 1920;
    encoderConfig.height = 1080;
    encoderConfig.framerate = 30;
    encoderConfig.bitrate = 4000000;
    encoderConfig.preset = videoCore::encode::EncoderConfig::Preset::UltraFast;

    auto startResult = pipeline_.initialize(captureConfig, encoderConfig);
    if (startResult != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Failed to initialize video pipeline\n";
        return false;
    }

    auto whipPublisherResult = whipPublisher_.initialize(
        whipUrl, streamKey, encoderConfig.framerate,
        [](const std::string &err) {
            std::cerr << "[CameraSession] WHIPPublisher error: " << err << "\n";
        });
    if (whipPublisherResult != networking::Result::Success) {
        std::cerr << "[CameraSession] Failed to initialize WHIP publisher\n";
        return false;
    }

    // Start the WHIP publisher first so it completes the ICE/DTLS handshake
    // and sets running_ = true before any frames are produced.  If the
    // pipeline starts first, every encoded packet is dropped while the WHIP
    // handshake is in progress (isRunning() == false), and the next keyframe
    // is not due for up to gopSize frames — causing the LiveKit ingress to
    // time out before receiving any data.
    auto publisherResult = whipPublisher_.start();
    if (publisherResult != networking::Result::Success) {
        std::cerr << "[CameraSession] Failed to start WHIP publisher\n";
        return false;
    }

    auto pipelineResult =
        pipeline_.start([this](std::unique_ptr<videoCore::Packet> pkt) {
            whipPublisher_.pushPacket(std::move(pkt));
        });

    if (pipelineResult != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Failed to start video pipeline\n";
        whipPublisher_.stop();
        return false;
    }

    isRunning_ = true;
    return true;
}

void CameraSession::stop() {
    if (!isRunning_) {
        return; // Not running
    }
    pipeline_.stop();
    whipPublisher_.stop();
    isRunning_ = false;
}