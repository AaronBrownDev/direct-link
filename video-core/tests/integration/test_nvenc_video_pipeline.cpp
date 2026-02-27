#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include "../../include/pipeline/video_pipeline.hpp"

int main() {
    videoCore::pipeline::VideoPipeline pipeline;

    videoCore::capture::CaptureConfig captureConfig;
    captureConfig.devicePath  = "/dev/video0";
    captureConfig.width       = 640;
    captureConfig.height      = 480;
    captureConfig.framerate   = 30;

    videoCore::encode::EncoderConfig encoderConfig;
    encoderConfig.width     = 640;
    encoderConfig.height    = 480;
    encoderConfig.framerate = 30;
    encoderConfig.bitrate   = 2000000;
    encoderConfig.type    = videoCore::encode::EncoderConfig::Type::Hardware;
    encoderConfig.preset  = videoCore::encode::EncoderConfig::Preset::UltraFast;

    std::cout << "Initializing pipeline...\n";
    auto result = pipeline.initialize(captureConfig, encoderConfig);
    if (result == videoCore::Result::ErrorDeviceNotFound) {
        std::cout << "SKIP: No GPU available, skipping NVENC test\n";
    return 0;
    }
    if (result != videoCore::Result::Success) {
        std::cerr << "FAIL: Unexpected init error: " 
            << videoCore::resultToString(result) << "\n";
        return 1;
    }

    std::atomic<int> packetsReceived = 0;

    std::cout << "Starting pipeline...\n";
    result = pipeline.start([&packetsReceived](std::unique_ptr<videoCore::Packet> pkt) {
        packetsReceived++;
        if (packetsReceived % 30 == 0) {
            std::cout << "Packets received: " << packetsReceived << "\n";
        }
    });

    if (result != videoCore::Result::Success) {
        std::cerr << "Failed to start: " 
                  << videoCore::resultToString(result) << "\n";
        return 1;
    }

    // Run for 5 seconds
    std::cout << "Running for 5 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Print stats before stopping
    std::cout << "\n=== Pipeline Stats ===\n";
    std::cout << "FPS:             " << pipeline.getFPS()              << "\n";
    std::cout << "Bitrate:         " << pipeline.getBitrate()          << " bps\n";
    std::cout << "Frames captured: " << pipeline.getFrameCount()       << "\n";
    std::cout << "Packets encoded: " << packetsReceived.load()         << "\n";

    float fps = pipeline.getFPS();

    std::cout << "\nStopping pipeline...\n";
    pipeline.stop();

    // Basic checks
    if (packetsReceived == 0) {
        std::cerr << "FAIL: No packets received\n";
        return 1;
    }
    if (fps < 20.0f) {
        std::cerr << "FAIL: FPS too low: " << fps << "\n";
        return 1;
    }

    std::cout << "PASS: Pipeline test complete\n";
    return 0;
}