#pragma once

#include "../../../networking/include/whip_publisher.hpp"
#include "../../../video-core/include/pipeline/video_pipeline.hpp"

#include <cstdint>

class CameraSession {
public:
    CameraSession() = default;
    ~CameraSession() = default;

    CameraSession(const CameraSession &) = delete;
    CameraSession &operator=(const CameraSession &) = delete;
    CameraSession(CameraSession &&) = delete;
    CameraSession &operator=(CameraSession &&) = delete;

    bool start(const std::string &whipUrl, const std::string &streamKey);
    void stop();
    void setPreviewCallback(std::function<void(const videoCore::Frame &)> cb);

    // Invoked from the encode thread for each packet handed to the WHIP
    // publisher. The pts is the encoded packet's pts (nanoseconds), which is
    // preserved unchanged from the input Frame's pts (set by CameraCapture
    // from the GStreamer buffer PTS). Used by the controller to fire latency
    // DC sends at the transmit rate rather than the capture rate, so DC and
    // video frame rates match at the receiver.
    void setPacketEncodedCallback(std::function<void(int64_t pts)> cb);

private:
    videoCore::pipeline::VideoPipeline pipeline_;
    networking::WHIPPublisher whipPublisher_;
    std::function<void(int64_t)> packetEncodedCb_;
    bool isRunning_ = false;
};
