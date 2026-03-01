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
    VideoPipeline() = default;
    ~VideoPipeline() = default;

    VideoPipeline(const VideoPipeline &) = delete;
    VideoPipeline &operator=(const VideoPipeline &) = delete;
    VideoPipeline(VideoPipeline &&) = delete;
    VideoPipeline &operator=(VideoPipeline &&) = delete;

    Result initialize(const capture::CaptureConfig &captureConfig,
                      const encode::EncoderConfig &encoderConfig);
    Result start(std::function<void(std::unique_ptr<Packet>)> packetCallback);
    Result stop();

    // Statistics getters
    [[nodiscard]] int getCurrentFramerate() const noexcept {
        return currentFramerate_;
    }
    [[nodiscard]] int getFrameCount() const noexcept { return frameCount_; }

    [[nodiscard]] int getBitrate() const {
        if (!encodeThread_.joinable()) {
            return 0;
        }
        auto elapsed = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - startTime_)
                           .count();
        return elapsed > 0.0 ? static_cast<int>((bitrate_ * 8) / elapsed) : 0;
    }

    [[nodiscard]] float getFPS() const {
        if (frameCount_ == 0) {
            return 0.0f;
        }
        auto elapsed = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - startTime_)
                           .count();
        return elapsed > 0.0 ? static_cast<float>(frameCount_ / elapsed) : 0.0f;
    }

    // NOLINTBEGIN(readability-convert-member-functions-to-static)
    [[nodiscard]] float getLatency() const {
        return 0.0f;
    } // NOLINTEND(readability-convert-member-functions-to-static)

private:
    encode::EncoderConfig encoderConfig_;
    std::unique_ptr<capture::CameraCapture> captureDevice_;
    std::unique_ptr<encode::Encoder> encoder_;
    std::function<void(std::unique_ptr<Packet>)> packetCallback_;

    // Frame queue  between capture and encode threads
    std::queue<std::unique_ptr<Frame>> frameQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // Encode thread
    std::jthread encodeThread_;

    // Statistics
    std::atomic<int> currentFramerate_ = 0;
    std::atomic<int> frameCount_ = 0;
    std::atomic<int> framesEncoded_ =
        0; // !TODO: add framesEncoded_ to stats and use it to calculate latency
    std::atomic<int> bitrate_ = 0;
    std::chrono::steady_clock::time_point startTime_;

    void encodeLoop(const std::stop_token &stopToken);
};
} // namespace videoCore::pipeline