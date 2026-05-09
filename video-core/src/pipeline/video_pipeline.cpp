#include "../../include/pipeline/video_pipeline.hpp"
#include "../../include/common/types.hpp"
#include "../../include/encode/encoder.hpp"

#include <iostream>

namespace videoCore::pipeline {

Result VideoPipeline::initialize(const capture::CaptureConfig &captureConfig,
                                 const encode::EncoderConfig &encoderConfig) {
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

Result VideoPipeline::start(
    std::function<void(std::unique_ptr<Packet>)> packetCallback) {
    if (!captureDevice_ || !encoder_) {
        return Result::ErrorInitFailed; // Not initialized
    }
    if (encodeThread_.joinable()) {
        return Result::ErrorInitFailed; // Already running
    }
    packetCallback_ = std::move(packetCallback);

    // Encoder packet callback; reused across the initial hardware attempt and
    // any software fallback so each path observes identical accounting.
    auto onEncodedPacket = [this](std::unique_ptr<Packet> pkt) {
        bitrate_ += pkt->size;
        framesEncoded_++;
        if (packetCallback_) {
            packetCallback_(std::move(pkt));
        }
    };

    // Try the initial encoder choice from createEncoder.  If it fails
    // (advertised at compile time, not actually loadable at runtime — common
    // when the FFmpeg build has NVENC compiled in but libcuda.so.1 is
    // missing, or has VAAPI but no DRI render node), fall through the
    // remaining hardware encoders before giving up to software.
    //
    // Using an explicit Type chain instead of relying on createEncoder's
    // own selection inside the retry, because `allowHardware=false` forces
    // Software directly and would skip VAAPI on hosts where NVENC was
    // probed first but isn't actually usable.
    auto encoder_result = encoder_->initialize(encoderConfig_, onEncodedPacket);
    if (encoder_result != Result::Success) {
        const encode::EncoderConfig::Type fallbackChain[] = {
            encode::EncoderConfig::Type::VAAPI,
            encode::EncoderConfig::Type::Software,
        };
        for (auto type : fallbackChain) {
            std::cerr << "[Pipeline] Encoder init failed; trying next "
                         "encoder in fallback chain\n";
            auto cfg = encoderConfig_;
            cfg.type = type;
            // allowHardware controls createEncoder's auto-upgrade rules:
            //  - VAAPI: pass true so the codec lookup runs but doesn't
            //    downgrade VAAPI to Software.
            //  - Software: pass false so createEncoder doesn't auto-upgrade
            //    Software→NVENC/VAAPI right back to a path we already failed.
            const bool allowHardware =
                type != encode::EncoderConfig::Type::Software;
            encoder_ = encode::createEncoder(cfg, allowHardware);
            if (!encoder_) {
                continue;
            }
            encoder_result = encoder_->initialize(cfg, onEncodedPacket);
            if (encoder_result == Result::Success) {
                break;
            }
            encoder_.reset();
        }
        if (!encoder_ || encoder_result != Result::Success) {
            return Result::ErrorInitFailed;
        }
    }

    startTime_ = std::chrono::steady_clock::now();

    auto capture_result =
        captureDevice_->start([this](std::unique_ptr<Frame> frame) {
            {
                if (previewCallback_) {
                    previewCallback_(*frame);
                }
                std::lock_guard<std::mutex> lock(
                    queueMutex_); // NOLINT(modernize-use-scoped-lock)
                if (frameQueue_.size() >= QUEUE_CAPACITY) {
                    frameQueue_.pop(); // Drop oldest frame
                    framesDropped_++;
                }
                frameQueue_.push(std::move(frame));
            }
            queueCondition_.notify_one();
            frameCount_++; // Counts all frames captured, including dropped ones
        });
    if (capture_result != Result::Success) {
        return capture_result; // Failed to start capture
    }

    encodeThread_ = std::jthread(
        [this](const std::stop_token &token) { encodeLoop(token); });

    return Result::Success;
}

void VideoPipeline::requestKeyframe() noexcept {
    if (encoder_) {
        encoder_->requestKeyframe();
    }
}

void VideoPipeline::setPreviewCallback(
    std::function<void(const Frame &)> previewCallback) {
    previewCallback_ = std::move(previewCallback);
}

Result VideoPipeline::stop() {
    if (captureDevice_ && captureDevice_->isRunning()) {
        captureDevice_->stop();
    }
    queueCondition_.notify_all();   // Wake up encode thread to exit
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
} // namespace videoCore::pipeline