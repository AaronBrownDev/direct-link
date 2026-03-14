#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace videoCore {
struct AVFrameDeleter {
    void operator()(::AVFrame *f) { av_frame_free(&f); }
};

struct AVPacketDeleter {
    void operator()(::AVPacket *f) { av_packet_free(&f); }
};

struct Frame {
    std::unique_ptr<::AVFrame, AVFrameDeleter> frame;
    int64_t pts = 0;
    int width = 0;
    int height = 0;
    AVPixelFormat format = AV_PIX_FMT_NONE;

    Frame() = default;
};

struct Packet {
    std::unique_ptr<::AVPacket, AVPacketDeleter> packet;
    int64_t pts = 0; // nanoseconds
    int64_t dts = 0; // nanoseconds
    int size = 0;
    bool isKeyframe = false;

    Packet() = default;
};

// Result type
enum class Result : std::uint8_t {
    Success,
    ErrorInvalidParameter,
    ErrorDeviceNotFound,
    ErrorInitFailed,
    ErrorEncodeFailed,
    ErrorCaptureFailed,
    ErrorDecodeFailed,
    ErrorNoData
};

inline std::string_view resultToString(Result result) {
    switch (result) {
    case Result::Success:
        return "Success";
    case Result::ErrorInvalidParameter:
        return "Invalid parameter";
    case Result::ErrorDeviceNotFound:
        return "Device not found";
    case Result::ErrorInitFailed:
        return "Initialization failed";
    case Result::ErrorEncodeFailed:
        return "Encode failed";
    case Result::ErrorCaptureFailed:
        return "Capture failed";
    case Result::ErrorNoData:
        return "No data available";
    default:
        return "Unknown error";
    }
}

} // namespace videoCore