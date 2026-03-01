#include "../../include/pipeline/video_pipeline.hpp"
#include "../../include/common/types.hpp"

namespace videoCore::pipeline {

    Result VideoPipeline::initialize(const capture::CaptureConfig& captureConfig, 
        const encode::EncoderConfig& encoderConfig) {
        encoderConfig_ = encoderConfig;
        
        // Initialize capture device
        captureDevice_ = std::make_unique<capture::CameraCapture>();
        auto capture_result = captureDevice_->initialize(captureConfig);
        if (capture_result != Result::Success) {
            return capture_result; // Failed to initialize capture device
        }

        // Initialize encoder
        encoder_ = encode::createEncoder(encoderConfig);
        if (!encoder_) {
            return Result::ErrorInitFailed; // Failed to create encoder
        }

        return Result::Success;
    }

    Result VideoPipeline::start(std::function<void(std::unique_ptr<Packet>)> packetCallback) {
        if (!captureDevice_ || !encoder_) {
            return Result::ErrorInitFailed; // Not initialized
        }
        if (encodeThread_.joinable()) {
            return Result::ErrorInitFailed; // Already running
        }
        packetCallback_ = std::move(packetCallback);

        // Initialize encoder with callback
        auto encoder_result = encoder_->initialize(encoderConfig_, [this](std::unique_ptr<Packet> pkt) {
            bitrate_ += pkt->size;
            framesEncoded_++;
            if (packetCallback_) { packetCallback_(std::move(pkt)); }
        });
        if (encoder_result != Result::Success) {
            return Result::ErrorInitFailed; // Failed to initialize encoder
        }

        startTime_ = std::chrono::steady_clock::now();

        auto capture_result = captureDevice_->start([this](std::unique_ptr<Frame> frame) {
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                frameQueue_.push(std::move(frame));
            }
            queueCondition_.notify_one();
            frameCount_++;
        });
        if (capture_result != Result::Success) {
            return capture_result; // Failed to start capture
        }

        encodeThread_ = std::jthread([this](const std::stop_token &token) {
            encodeLoop(token);
        });

        return Result::Success;
    }

    Result VideoPipeline::stop() {
        if (captureDevice_ && captureDevice_->isRunning()) {
            captureDevice_->stop();
        }
        queueCondition_.notify_all(); // Wake up encode thread to exit
        encodeThread_ = std::jthread(); // Join encode thread

        if (encoder_) {
            encoder_->stop();
        }
        return Result::Success;
    }

    void VideoPipeline::encodeLoop(const std::stop_token &stopToken) {
        while (!stopToken.stop_requested()) {
            std::unique_ptr<Frame> frame;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCondition_.wait(lock, [this, &stopToken] {
                    return !frameQueue_.empty() || stopToken.stop_requested();
                });
                if (stopToken.stop_requested() && frameQueue_.empty()) {
                    break; // Exit loop if stopping and no frames left
                }
                frame = std::move(frameQueue_.front());
                frameQueue_.pop();
            }
            if (frame) {
                encoder_->encodeFrame(frame->frame.get());
            }
        }
    }
}