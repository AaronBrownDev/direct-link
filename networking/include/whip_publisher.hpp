#pragma once

#include "../../video-core/include/common/types.hpp"
#include "types.hpp"
#include <functional>
#include <memory>
#include <string>

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
    std::string whipUrl_;
    std::string streamKey_;
    std::function<void(std::string)> onErrorCallback_;
    bool running_ = false;
    GstElement *pipeline_ = nullptr;
    GstElement *appsrc_ = nullptr;
    std::uint64_t frameCount_ = 0;
    int framerate_ = 60; // Default framerate, can be overridden by config
};
} // namespace networking