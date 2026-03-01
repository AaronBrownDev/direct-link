#pragma once

#include "../common/types.hpp"
#include "capture_config.hpp"
#include <functional>
#include <thread>

struct AVFormatContext;
struct AVCodecContext;

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

    [[nodiscard]] bool isRunning() const { return captureThread_.joinable(); }
    [[nodiscard]] int getWidth() const;
    [[nodiscard]] int getHeight() const;
    [[nodiscard]] int getFramerate() const;

private:
    CaptureConfig config_;
    AVFormatContext *formatCtx_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    int videoStreamIdx_ = -1;
    std::jthread captureThread_;

    std::function<void(std::unique_ptr<Frame>)> frameCallback_;

    void captureLoop(const std::stop_token &stopToken);
    Result setupDevice();
    Result setupCodec();
};
} // namespace videoCore::capture