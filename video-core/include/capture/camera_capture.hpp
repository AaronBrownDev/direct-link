#pragma once

#include "../common/types.hpp"
#include "capture_config.hpp"
#include <atomic>
#include <functional>
#include <thread>

typedef struct _GstElement GstElement;  // NOLINT(modernize-use-using, bugprone-reserved-identifier)
namespace videoCore::capture {

class CameraCapture {
public:
    CameraCapture() = default;
    ~CameraCapture();
    CameraCapture(CameraCapture &&) = delete;
    CameraCapture &operator=(CameraCapture &&) = delete;
    CameraCapture(const CameraCapture &) = delete;
    CameraCapture &operator=(const CameraCapture &) = delete;

    Result initialize(const CaptureConfig &config);
    Result start(std::function<void(std::unique_ptr<Frame>)> frameCallback);
    Result stop();

    [[nodiscard]] bool isRunning() const {
        return running_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int getWidth() const { return width_; }
    [[nodiscard]] int getHeight() const { return height_; }
    [[nodiscard]] int getFramerate() const { return framerate_; }

private:
    CaptureConfig config_;
    GstElement *pipeline_ = nullptr;
    GstElement *appsink_ = nullptr;

    std::atomic<bool> running_{false};
    std::jthread captureThread_;

    int width_ = 0;
    int height_ = 0;
    int framerate_ = 0;

    std::function<void(std::unique_ptr<Frame>)> frameCallback_;

    [[nodiscard]] Result buildPipeline();
    void captureLoop(const std::stop_token &stopToken);
};

} // namespace videoCore::capture