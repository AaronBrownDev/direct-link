#pragma once

#include "types.hpp"
#include "../../video-core/include/common/types.hpp"
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
                      std::function<void(std::string)> onErrorCallback);
    Result start();
    Result stop();

    void pushPacket(std::unique_ptr<videoCore::Frame> packet);
    bool isRunning() const;
};
} // namespace networking