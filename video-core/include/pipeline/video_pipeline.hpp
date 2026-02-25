#pragma once

#include "../capture/camera_capture.hpp"
#include "../capture/capture_config.hpp"
#include "../encode/encoder.hpp"
#include "../encode/encoder_config.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace videoCore::pipeline {
class VideoPipeline {
public:
    VideoPipeline()  = default;
    ~VideoPipeline() = default;

    VideoPipeline(const VideoPipeline&)            = delete;
    VideoPipeline& operator=(const VideoPipeline&) = delete;

    Result initialize(const capture::CaptureConfig& captureConfig, 
        const encode::EncoderConfig& encoderConfig);
    Result start(std::function<void(std::unique_ptr<Packet>)> packetCallback);
    Result stop();

    // Statistics getters
    [[nodiscard]] int getCurrentFramerate() const noexcept { return currentFramerate_; }
    [[nodiscard]] int getFrameCount() const noexcept { return frameCount_; }
    [[nodiscard]] int getBitrate() const noexcept { return bitrate_; }

    [[nodiscard]] float getFPS() const {
        auto elapsed = std::chrono::steady_clock::now() - startTime_;
        double seconds = std::chrono::duration<double>(elapsed).count();
        return seconds > 0.0 ? static_cast<float>(frameCount_ / seconds) : 0.0f;
    }

    [[nodiscard]] float getLatency() const { return 0.0f;};

private:
    std::unique_ptr<capture::CameraCapture> captureDevice_;
    std::unique_ptr<encode::Encoder> encoder_;
    std::function<void(std::unique_ptr<Packet>)> PacketCallback_;

    // Frame queue  between capture and encode threads
    std::queue<std::unique_ptr<Frame>> frameQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // Encode thread
    std::jthread encodeThread_;

    // Statistics
    std::atomic<int> currentFramerate_ = 0;
    std::atomic<int> frameCount_ = 0;
    std::atomic<int> bitrate_ = 0;
    std::chrono::steady_clock::time_point startTime_;

    void encodeLoop(std::stop_token stopToken);
};
}