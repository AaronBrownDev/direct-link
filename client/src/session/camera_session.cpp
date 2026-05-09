#include "camera_session.hpp"
#include "../../../video-core/include/capture/camera_enumerator.hpp"
#include "../../../video-core/include/common/types.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

bool CameraSession::start(const std::string &whipUrl,
                          const std::string &streamKey,
                          const std::string &deviceId) {
    if (isRunning_) {
        return false; // Already running
    }

    videoCore::capture::CaptureConfig captureConfig;

#ifdef _WIN32
    // Windows still uses the legacy hard-coded path; DirectShow enumeration
    // would slot in here once we add it to CameraEnumerator.
    captureConfig.devicePath = "video=0";
    captureConfig.inputFormat = "dshow";
    captureConfig.width = 1280;
    captureConfig.height = 720;
    captureConfig.framerate = 30;
#else
    using videoCore::capture::CameraDevice;
    using videoCore::capture::CameraEnumerator;
    using videoCore::capture::CameraFormat;

    std::optional<CameraDevice> device;
    if (deviceId.empty()) {
        device = CameraEnumerator::pickDefaultDevice();
        if (!device) {
            std::cerr << "[CameraSession] no camera devices detected\n";
            return false;
        }
    }
    else {
        for (auto &d : CameraEnumerator::listDevices()) {
            if (d.id == deviceId) {
                device = std::move(d);
                break;
            }
        }
        if (!device) {
            std::cerr << "[CameraSession] camera device not found: "
                      << deviceId << "\n";
            return false;
        }
    }

    std::optional<CameraFormat> format =
        CameraEnumerator::pickBestFormat(device->formats);
    if (!format) {
        // Device exposed no parseable formats — fall back to a sensible
        // default; downstream candidate probing will still validate it.
        std::cerr << "[CameraSession] no formats advertised for "
                  << device->displayName << "; falling back to 1280x720@30\n";
        format = CameraFormat{
            "image/jpeg", 1280, 720, 30, 1,
        };
    }

    std::cerr << "[CameraSession] using " << device->displayName << " ("
              << device->id << ") @ " << format->width << "x" << format->height
              << " " << format->fps() << "fps " << format->mediaType << "\n";

    captureConfig.devicePath = device->id;
    captureConfig.inputFormat = device->source;
    captureConfig.pixelFormat =
        format->mediaType == "image/jpeg" ? "mjpeg" : "raw";
    captureConfig.width = format->width;
    captureConfig.height = format->height;
    // Round framerate to the nearest integer; the encoder timebase only
    // accepts whole frames per second.
    captureConfig.framerate =
        format->framerateDen > 0
            ? static_cast<int>(std::lround(format->fps()))
            : 30;
    if (captureConfig.framerate <= 0) {
        captureConfig.framerate = 30;
    }
#endif

    videoCore::encode::EncoderConfig encoderConfig;
    encoderConfig.width = captureConfig.width;
    encoderConfig.height = captureConfig.height;
    encoderConfig.framerate = captureConfig.framerate;

    // Scale bitrate with pixel-rate.  ~0.1 bit per pixel per second is the
    // commonly recommended floor for camera-style content at H.264 baseline;
    // anything substantially below that produces visible compression
    // artefacts and large IDRs that fragment poorly over WebRTC.  4 Mbps
    // works for 720p30 (27M pix/s) but starves 1080p60 (124M pix/s).  Cap
    // at 12 Mbps so we don't saturate typical home upstream pipes.
    const long long pixelRate = static_cast<long long>(captureConfig.width) *
                                captureConfig.height *
                                captureConfig.framerate;
    long long bitrate = pixelRate / 10;  // 0.1 bit/pixel/sec
    if (bitrate < 2'000'000) {
        bitrate = 2'000'000;
    }
    if (bitrate > 12'000'000) {
        bitrate = 12'000'000;
    }
    encoderConfig.bitrate = static_cast<int>(bitrate);

    // ~0.5 s between keyframes regardless of framerate.
    encoderConfig.gopSize = std::max(1, captureConfig.framerate / 2);
    encoderConfig.preset = videoCore::encode::EncoderConfig::Preset::UltraFast;

    auto startResult = pipeline_.initialize(captureConfig, encoderConfig);
    if (startResult != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Failed to initialize video pipeline: "
                  << videoCore::resultToString(startResult)
                  << " (device=" << captureConfig.devicePath
                  << ", format=" << captureConfig.inputFormat
                  << ", pixel_format=" << captureConfig.pixelFormat
                  << ", " << captureConfig.width << "x" << captureConfig.height
                  << "@" << captureConfig.framerate << "fps)\n";
        return false; // Failed to initialize pipeline
    }
    
    auto whipPublisherResult = whipPublisher_.initialize(
        whipUrl, streamKey, encoderConfig.framerate,
        [](const std::string &err) {
            std::cerr << "[CameraSession] WHIPPublisher error: " << err << "\n";
        });
    if (whipPublisherResult != networking::Result::Success) {
        std::cerr << "[CameraSession] Failed to initialize WHIP publisher"
                     " (url=" << whipUrl << ")\n";
        return false;
    }

     whipPublisher_.setKeyframeRequestCallback([this]() {
        pipeline_.requestKeyframe();
    });

    // The WHIP publisher is started first to set running_ to true 
    // before producing frames to avoid dropping frames during
    // publisher startup.
    auto publisherResult = whipPublisher_.start();
    if (publisherResult != networking::Result::Success) {
        std::cerr << "[CameraSession] Failed to start WHIP publisher";
        pipeline_.stop();
        return false;
    }

    auto pipelineResult =
        pipeline_.start([this](std::unique_ptr<videoCore::Packet> pkt) {
            // Notify upstream listeners (e.g. CameraSessionController) that a
            // packet was just produced by the encoder, so they can drive
            // capture-rate-locked work — like sending the latency DC packet —
            // off the transmit rate instead of the capture rate.  Done before
            // moving pkt into pushPacket so we still have access to its pts.
            if (packetEncodedCb_) {
                packetEncodedCb_(pkt->pts);
            }
            whipPublisher_.pushPacket(std::move(pkt));
        });

    if (pipelineResult != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Failed to start video pipeline: "
                  << videoCore::resultToString(pipelineResult) << "\n";
        whipPublisher_.stop();
        return false;
    }

    isRunning_ = true;
    return true;
}

void CameraSession::setPreviewCallback(std::function<void(const videoCore::Frame &)> cb) {
    pipeline_.setPreviewCallback(std::move(cb));
}

void CameraSession::setPacketEncodedCallback(std::function<void(int64_t)> cb) {
    packetEncodedCb_ = std::move(cb);
}

void CameraSession::stop() {
    if (!isRunning_) {
        return; // Not running
    }
    const auto stop_result = pipeline_.stop();
    if (stop_result != videoCore::Result::Success) {
        std::cerr << "[CameraSession] Pipeline stop returned an error: "
                  << videoCore::resultToString(stop_result) << "\n";
    }
    whipPublisher_.stop();
    isRunning_ = false;
}