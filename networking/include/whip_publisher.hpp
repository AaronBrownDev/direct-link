#pragma once

#include "../../video-core/include/common/types.hpp"
#include "types.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
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

    // Register a callback invoked when the remote decoder sends a RTCP PLI or
    // FIR (via GstForceKeyUnitEvent).  The callback should request an IDR from
    // the encoder.  Must be called before start().
    void setKeyframeRequestCallback(std::function<void()> cb) noexcept {
        forceKeyframeCallback_ = std::move(cb);
    }

    // Latest camera-side per-packet send delay in milliseconds, measured via
    // a GStreamer pad probe at the post-pacer point inside webrtcbin.  Each
    // probed buffer's PTS (set by do-timestamp=TRUE at appsrc push) is
    // compared to the pipeline's current running time at the probe point;
    // the difference is the wall-clock dwell time of that buffer in the
    // send pipeline (h264parse + rtph264pay + webrtcbin pacer + RTX/NACK
    // retransmit buffer).  Returned value is a rolling mean over the last
    // SEND_DELAY_WINDOW probes.  Reads are safe from any thread.
    [[nodiscard]] double senderPacketDelayMs() const noexcept {
        return last_send_delay_ms_.load(std::memory_order_relaxed);
    }

private:
    void logBusError();
    // Locate rtpbin inside webrtcbin and hook its "pad-added" signal so we
    // can attach the buffer probe to send_rtp_src_* the moment it appears.
    // Called once the pipeline reaches PLAYING; idempotent.
    void setupSendDelayProbeListener();
    // Attach the buffer probe to a pad we know is the post-pacer point.
    // Called from the pad-added signal handler, and (for safety) once from
    // setupSendDelayProbeListener if the pad already exists at hook time.
    void attachSendDelayProbe(GstPad *pad);
    // Static trampolines for GLib signal / pad-probe callbacks.
    static void onRtpbinPadAdded(GstElement *rtpbin, GstPad *new_pad, gpointer user_data);
    static GstPadProbeReturn sendDelayProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

    std::string whipUrl_;
    std::string streamKey_;
    std::function<void(std::string)> onErrorCallback_;
    std::function<void()> forceKeyframeCallback_;
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

    // Send-delay probe state.  delay_sum_ns_ + delay_count_ form a rolling
    // sum/count over the last SEND_DELAY_WINDOW probes; the probe rotates
    // the window every SEND_DELAY_WINDOW samples by halving both.  Stored
    // as atomics because the probe runs on whichever thread is sending
    // packets, while readers can be on any thread.  delay_probe_pad_ is
    // retained so stop() can detach the probe before tearing down.
    std::atomic<double> last_send_delay_ms_{0.0};
    std::uint64_t delay_sum_ns_ = 0;
    std::uint32_t delay_count_ = 0;
    // rtpbin we hooked pad-added on, retained so stop() can disconnect.
    GstElement *delay_rtpbin_ = nullptr;
    gulong delay_pad_added_id_ = 0;
    // The probe pad we attached to, retained so stop() can remove the probe.
    GstPad *delay_probe_pad_ = nullptr;
    gulong delay_probe_id_ = 0;
    static constexpr std::uint32_t SEND_DELAY_WINDOW = 60; // ~2 s at 30 fps
};
} // namespace networking