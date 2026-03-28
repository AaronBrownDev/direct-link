#pragma once

#include "../../video-core/include/common/types.hpp"
#include "types.hpp"
#include <functional>
#include <memory>
#include <string>
#include <gst/gst.h>

namespace networking {
class WHIPPublisher {
public:
    WHIPPublisher() = default;
    ~WHIPPublisher();
    WHIPPublisher(const WHIPPublisher &) = delete;
    WHIPPublisher &operator=(const WHIPPublisher &) = delete;
    WHIPPublisher(WHIPPublisher &&) = delete;
    WHIPPublisher &operator=(WHIPPublisher &&) = delete;

    Result initialize(const std::string &whip_url,
                      const std::string &stream_key,
                      int framerate,
                      std::function<void(std::string)> onErrorCallback);
    Result start();
    Result stop();

    void pushPacket(std::unique_ptr<videoCore::Packet> packet);
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

private:
    void logBusError();

    std::string whipUrl_;
    std::string streamKey_;
    std::function<void(std::string)> onErrorCallback_;
    bool running_ = false;
    GstElement *pipeline_ = nullptr;
    GstElement *appsrc_ = nullptr;
    std::mutex appsrcMutex_;
    std::uint64_t frameCount_ = 0;
    int framerate_ = 60; // Default framerate, can be overridden by config
    // Absolute PTS of the first packet pushed; used to compute pipeline-relative
    // timestamps.  v4l2 timestamps are relative to device open, not the
    // GStreamer pipeline base time, so raw PTS values would cause GStreamer to
    // buffer packets until the pipeline clock catches up.
    std::int64_t streamStartPts_ = -1;
};
} // namespace networking