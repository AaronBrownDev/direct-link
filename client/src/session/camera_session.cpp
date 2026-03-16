#include "camera_session.hpp"
#include <iostream>

bool CameraSession::start(const std::string &whipUrl,
                          const std::string &streamKey) {
    if (isRunning_) {
        return false; // Already running
    }

    videoCore::capture::CaptureConfig captureConfig;
    captureConfig.devicePath = "/dev/video0";
    captureConfig.inputFormat = "v4l2";
    captureConfig.width = 1920;
    captureConfig.height = 1080;
    captureConfig.framerate = 30;

    videoCore::encode::EncoderConfig encoderConfig;
    encoderConfig.width = 1920;
    encoderConfig.height = 1080;
    encoderConfig.framerate = 30;
    encoderConfig.bitrate = 4000000;
    encoderConfig.preset = videoCore::encode::EncoderConfig::Preset::UltraFast;

    auto startResult = pipeline_.initialize(captureConfig, encoderConfig);
    if (startResult != videoCore::Result::Success) {
        return false; // Failed to initialize pipeline
    }
    
    auto whipPublisherResult = whipPublisher_.initialize(
        whipUrl, streamKey, encoderConfig.framerate,
        [](const std::string &err) {
            std::cerr << "WHIPPublisher error: " << err << "\n";
        });
    if (whipPublisherResult != networking::Result::Success) {
        return false; // Failed to initialize WHIP publisher
    }

    auto pipelineResult =
        pipeline_.start([this](std::unique_ptr<videoCore::Packet> pkt) {
            whipPublisher_.pushPacket(std::move(pkt));
        });

    if (pipelineResult != videoCore::Result::Success) {
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