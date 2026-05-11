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

    // deviceId is optional; when empty the default camera (first v4l2 device
    // with parseable formats) is auto-selected.  When non-empty it must
    // match a CameraDevice::id returned by CameraEnumerator::listDevices().
    bool start(const std::string &whipUrl, const std::string &streamKey,
               const std::string &deviceId = "");
    void stop();
    void setPreviewCallback(std::function<void(const videoCore::Frame &)> cb);

    // Invoked from the encode thread for each packet handed to the WHIP
    // publisher. The pts is the encoded packet's pts (nanoseconds), which is
    // preserved unchanged from the input Frame's pts (set by CameraCapture
    // from the GStreamer buffer PTS). Used by the controller to fire latency
    // DC sends at the transmit rate rather than the capture rate, so DC and
    // video frame rates match at the receiver.
    void setPacketEncodedCallback(std::function<void(int64_t pts)> cb);

    // Latest camera-side per-packet send delay reported by WHIPPublisher's
    // rtpbin send-pad probe (rolling mean over the most recent window of
    // probed buffers).  Captures pacing + RTX/NACK retransmit dwell time
    // that the director's JB stat cannot see.  Returns 0 before the first
    // probe fires.  See WHIPPublisher::senderPacketDelayMs.
    [[nodiscard]] double senderPacketDelayMs() const noexcept {
        return whipPublisher_.senderPacketDelayMs();
    }

private:
    videoCore::pipeline::VideoPipeline pipeline_;
    networking::WHIPPublisher whipPublisher_;
    std::function<void(int64_t)> packetEncodedCb_;
    bool isRunning_ = false;
};
