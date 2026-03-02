#include "../../include/pipeline/video_pipeline.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    videoCore::pipeline::VideoPipeline pipeline;

    videoCore::capture::CaptureConfig capture_config;
    capture_config.devicePath = "/dev/video0";
    capture_config.width = 640;
    capture_config.height = 480;
    capture_config.framerate = 30;

    videoCore::encode::EncoderConfig encoder_config;
    encoder_config.width = 640;
    encoder_config.height = 480;
    encoder_config.framerate = 30;
    encoder_config.bitrate = 2000000;
    encoder_config.preset = videoCore::encode::EncoderConfig::Preset::UltraFast;

    std::cout << "Initializing pipeline...\n";
    auto result = pipeline.initialize(capture_config, encoder_config);
    if (result != videoCore::Result::Success) {
        std::cerr << "Failed to initialize: "
                  << videoCore::resultToString(result) << "\n";
        return 1;
    }

    std::atomic<int> packets_received = 0;

    std::cout << "Starting pipeline...\n";
    result = pipeline.start(
        [&packets_received](std::unique_ptr<videoCore::Packet> pkt) {
            packets_received++;
            if (packets_received % 30 == 0) {
                std::cout << "Packets received: " << packets_received << "\n";
            }
        });

    if (result != videoCore::Result::Success) {
        std::cerr << "Failed to start: " << videoCore::resultToString(result)
                  << "\n";
        return 1;
    }

    // Run for 5 seconds
    std::cout << "Running for 5 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Print stats before stopping
    std::cout << "\n=== Pipeline Stats ===\n";
    std::cout << "FPS:             " << pipeline.getFPS() << "\n";
    std::cout << "Bitrate:         " << pipeline.getBitrate() << " bps\n";
    std::cout << "Frames captured: " << pipeline.getFrameCount() << "\n";
    std::cout << "Frames dropped:  " << pipeline.getDroppedFrames() << "\n";
    std::cout << "Packets encoded: " << packets_received.load() << "\n";

    float fps = pipeline.getFPS();

    std::cout << "\nStopping pipeline...\n";
    pipeline.stop();

    // Basic checks
    if (packets_received == 0) {
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