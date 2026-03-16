#pragma once

#include "../../../networking/include/whip_publisher.hpp"
#include "../../../video-core/include/pipeline/video_pipeline.hpp"

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

private:
    videoCore::pipeline::VideoPipeline pipeline_;
    networking::WHIPPublisher whipPublisher_;
    bool isRunning_ = false;
};
