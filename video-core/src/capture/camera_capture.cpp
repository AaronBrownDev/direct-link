#include "../../include/capture/camera_capture.hpp"
#include "../../include/capture/capture_config.hpp"
#include <cstring>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <iostream>
#include <string>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,
// bugprone-casting-through-void,
// cppcoreguidelines-pro-bounds-constant-array-index,
// clang-analyzer-optin.core.EnumCastOutOfRange,
// readability-implicit-bool-conversion)
namespace videoCore::capture {

CameraCapture::~CameraCapture() {
    stop();
    if (appsink_ != nullptr) {
        gst_object_unref(appsink_);
        appsink_ = nullptr;
    }
    if (pipeline_ != nullptr) {
        // Always transition to NULL before unreffing. If start() never ran (or
        // failed mid-way), the pipeline may still own live GStreamer threads
        // that would keep the process alive after the window is closed.
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

Result CameraCapture::initialize(const CaptureConfig &config) {
    config_ = config;

    // Apply defaults for unspecified dimensions/framerate.
    if (config_.width == 0) {
        config_.width = 1280;
    }
    if (config_.height == 0) {
        config_.height = 720;
    }
    if (config_.framerate == 0) {
        config_.framerate = 30;
    }

    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    return buildPipeline();
}

// Drain the GStreamer bus and log the first ERROR message, if any.
// Uses a 500 ms timed wait because some sources (notably pipewiresrc) post
// errors asynchronously — a non-blocking pop races with delivery and silently
// misses the message.
static void logPipelineError(GstElement *pipeline) {
    GstBus *bus = gst_element_get_bus(pipeline);
    if (bus == nullptr) {
        return;
    }
    GstMessage *msg =
        gst_bus_timed_pop_filtered(bus, 500 * GST_MSECOND, GST_MESSAGE_ERROR);
    if (msg != nullptr) {
        GError *err = nullptr;
        gchar *dbg = nullptr;

        gst_message_parse_error(msg, &err, &dbg);
        std::cerr << "[CameraCapture] pipeline error: "
                  << (err != nullptr ? err->message : "?");
        if (dbg != nullptr) {
            std::cerr << " | " << dbg;
        }
        std::cerr << "\n";
        if (err != nullptr) {
            g_error_free(err);
        }
        if (dbg != nullptr) {
            g_free(dbg);
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}

Result CameraCapture::buildPipeline() {
    const int w = config_.width;
    const int h = config_.height;
    const int f = config_.framerate;
    const std::string dev = config_.devicePath;

    const std::string raw_caps = "video/x-raw,format=I420"
                                 ",width=" +
                                 std::to_string(w) +
                                 ",height=" + std::to_string(h) +
                                 ",framerate=" + std::to_string(f) + "/1";
    const std::string sink_props = "appsink name=sink sync=false max-buffers=4 "
                                   "drop=true emit-signals=false";

    // Ordered candidates.  Each candidate carries the GstState it should be
    // probed to; v4l2src candidates are additionally validated by pulling a
    // sample at PLAYING because v4l2 may advertise a format (e.g. MJPEG) that
    // it doesn't actually deliver.  OBS Virtual Camera is the canonical
    // offender: V4L2_QUERYCAP claims MJPEG is supported, PAUSED negotiates
    // cleanly, but the streaming thread immediately errors with
    // "not-negotiated" once PLAYING begins.
    //
    // Probe state rationale:
    //  - v4l2src is probed to PLAYING + first-sample.  READY/PAUSED are too
    //    lenient: they don't catch streaming-time format mismatches.  v4l2
    //    cleanup from PLAYING back to NULL is reliable, so this is safe.
    //  - pipewiresrc is probed to READY only.  Going further can hang
    //    indefinitely when WirePlumber has registered no video nodes; the
    //    real liveness check is deferred to start() under a 5 s timeout.
    //
    // Candidate ordering rationale:
    //  - v4l2src comes first: it is universally compatible and works whether
    //    PipeWire is present or absent (via the pipewire-v4l2 compat layer).
    //  - pipewiresrc variants are kept as fallbacks for environments where
    //    native PipeWire video nodes are available and v4l2 compat is disabled.
    struct Candidate {
        std::string pipeline;
        GstState probeState;
        bool validateSample;  // pull a sample at PLAYING to confirm streaming
    };
    std::vector<Candidate> candidates;

    // V4L2 MJPEG — preferred; most USB cameras expose MJPEG at 720p+/30fps.
    candidates.push_back(
        {"v4l2src device=" + dev + " ! image/jpeg,width=" + std::to_string(w) +
             ",height=" + std::to_string(h) + ",framerate=" + std::to_string(f) +
             "/1"
             " ! jpegdec ! videoconvert ! video/x-raw,format=I420 ! " +
             sink_props,
         GST_STATE_PLAYING, /*validateSample=*/true});

    // V4L2 raw — fallback for cameras without MJPEG (OBS Virtual Camera and
    // many integrated webcams expose only YUYV/NV12).  videoconvert will
    // negotiate the actual source format on the v4l2src side.
    candidates.push_back(
        {"v4l2src device=" + dev + " ! video/x-raw,width=" + std::to_string(w) +
             ",height=" + std::to_string(h) +
             ",framerate=" + std::to_string(f) +
             "/1"
             " ! videoconvert ! video/x-raw,format=I420 ! " +
             sink_props,
         GST_STATE_PLAYING, /*validateSample=*/true});

    GstElementFactory *pw_factory = gst_element_factory_find("pipewiresrc");
    if (pw_factory != nullptr) {
        gst_object_unref(pw_factory);
        candidates.push_back({"pipewiresrc target-object=" + dev +
                                  " do-timestamp=true"
                                  " ! jpegdec ! videoconvert ! videoscale ! " +
                                  raw_caps + " ! " + sink_props,
                              GST_STATE_READY, /*validateSample=*/false});
        candidates.push_back({"pipewiresrc do-timestamp=true"
                              " ! jpegdec ! videoconvert ! videoscale ! " +
                                  raw_caps + " ! " + sink_props,
                              GST_STATE_READY, /*validateSample=*/false});
        candidates.push_back({"pipewiresrc target-object=" + dev +
                                  " do-timestamp=true"
                                  " ! videoconvert ! videoscale ! " +
                                  raw_caps + " ! " + sink_props,
                              GST_STATE_READY, /*validateSample=*/false});
        candidates.push_back({"pipewiresrc do-timestamp=true"
                              " ! videoconvert ! videoscale ! " +
                                  raw_caps + " ! " + sink_props,
                              GST_STATE_READY, /*validateSample=*/false});
    }

    for (const auto &candidate : candidates) {
        std::cerr << "[CameraCapture] trying: " << candidate.pipeline << "\n";

        GError *parse_err = nullptr;
        GstElement *pipeline =
            gst_parse_launch(candidate.pipeline.c_str(), &parse_err);
        if (parse_err != nullptr || pipeline == nullptr) {
            std::cerr << "[CameraCapture] parse error: "
                      << (parse_err != nullptr ? parse_err->message
                                               : "null pipeline")
                      << "\n";
            if (parse_err != nullptr) {
                g_error_free(parse_err);
            }
            if (pipeline != nullptr) {
                gst_object_unref(pipeline);
            }
            continue;
        }

        const GstState target = candidate.probeState;
        GstStateChangeReturn ret = gst_element_set_state(pipeline, target);
        GstState reached = GST_STATE_NULL;
        // PLAYING probes need the longest wait — they negotiate caps,
        // preroll, and start the streaming thread.  READY probes return
        // effectively synchronously.
        const GstClockTime timeout =
            (target == GST_STATE_PLAYING ? 5 * GST_SECOND : GST_SECOND);
        if (ret != GST_STATE_CHANGE_FAILURE) {
            ret = gst_element_get_state(pipeline, &reached, nullptr, timeout);
        }

        bool ok = (ret != GST_STATE_CHANGE_FAILURE && reached == target);
        GstElement *appsink = nullptr;
        if (ok) {
            appsink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
            if (appsink == nullptr) {
                std::cerr << "[CameraCapture] appsink element not found in "
                             "pipeline\n";
                ok = false;
            }
        }

        // Streaming-time validation for v4l2src: pull a sample to confirm the
        // negotiated format actually flows.  A camera can advertise a format
        // (e.g. OBS Virtual Camera advertising MJPEG) that PAUSED accepts but
        // PLAYING rejects with "not-negotiated"; only an actual buffer pull
        // exposes the lie.
        if (ok && candidate.validateSample) {
            GstSample *sample = gst_app_sink_try_pull_sample(
                GST_APP_SINK(appsink), 2 * GST_SECOND);
            if (sample == nullptr) {
                // Drain the bus to surface the underlying GStreamer error.
                logPipelineError(pipeline);
                std::cerr << "[CameraCapture] no sample within 2s — "
                             "candidate cannot stream\n";
                ok = false;
            }
            else {
                gst_sample_unref(sample);
            }
        }

        if (ok) {
            pipeline_ = pipeline;
            appsink_ = appsink;
            width_ = w;
            height_ = h;
            framerate_ = f;
            std::cerr << "[CameraCapture] pipeline ready\n";
            return Result::Success;
        }

        // Candidate didn't pass.  Drain the bus for a diagnostic if we
        // haven't already (logPipelineError ran for the validateSample
        // failure path), tear down, and move to the next candidate.
        if (!candidate.validateSample) {
            logPipelineError(pipeline);
        }
        if (appsink != nullptr) {
            gst_object_unref(appsink);
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }

    std::cerr << "[CameraCapture] all pipeline candidates failed\n";
    return Result::ErrorDeviceNotFound;
}

Result CameraCapture::start(
    std::function<void(std::unique_ptr<Frame>)> frameCallback) {
    if (pipeline_ == nullptr) {
        return Result::ErrorInitFailed;
    }
    if (running_.load(std::memory_order_relaxed) || captureThread_.joinable()) {
        return Result::ErrorInitFailed;
    }

    frameCallback_ = std::move(frameCallback);

    GstStateChangeReturn ret =
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        logPipelineError(pipeline_);
        std::cerr << "[CameraCapture] failed to set pipeline to PLAYING\n";
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        return Result::ErrorCaptureFailed;
    }

    // Wait up to 5 s for the pipeline to reach PLAYING.
    GstState state = GST_STATE_NULL;
    ret = gst_element_get_state(pipeline_, &state, nullptr,
                                static_cast<GstClockTime>(5) * GST_SECOND);
    if (ret == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PLAYING) {
        logPipelineError(pipeline_);
        std::cerr << "[CameraCapture] pipeline failed to reach PLAYING state "
                     "(reached: "
                  << gst_element_state_get_name(state) << ")\n";
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        return Result::ErrorCaptureFailed;
    }

    running_.store(true, std::memory_order_relaxed);
    captureThread_ = std::jthread(
        [this](const std::stop_token &token) { captureLoop(token); });

    return Result::Success;
}

Result CameraCapture::stop() {
    if (!running_.load(std::memory_order_relaxed)) {
        return Result::Success;
    }

    running_.store(false, std::memory_order_relaxed);

    if (captureThread_.joinable()) {
        captureThread_.request_stop();
        captureThread_.join();
    }

    if (pipeline_ != nullptr) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    return Result::Success;
}

void CameraCapture::captureLoop(const std::stop_token &stopToken) {
    // 100 ms pull timeout expressed in nanoseconds.
    constexpr GstClockTime k_pull_timeout_ns = 100 * GST_MSECOND;

    while (!stopToken.stop_requested()) {
        GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_),
                                                         k_pull_timeout_ns);

        if (sample == nullptr) {
            // Timeout or EOS — check the bus for a hard error before looping.
            GstBus *bus = gst_element_get_bus(pipeline_);
            if (bus != nullptr) {
                GstMessage *msg = gst_bus_pop_filtered(
                    bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR |
                                                     GST_MESSAGE_EOS));
                if (msg != nullptr) {
                    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                        GError *err = nullptr;
                        gchar *dbg = nullptr;
                        gst_message_parse_error(msg, &err, &dbg);
                        std::cerr << "[CameraCapture] pipeline error: "
                                  << (err != nullptr ? err->message : "unknown")
                                  << " | " << (dbg != nullptr ? dbg : "")
                                  << "\n";
                        if (err != nullptr) {
                            g_error_free(err);
                        }
                        if (dbg != nullptr) {
                            g_free(dbg);
                        }
                    }
                    gst_message_unref(msg);
                    gst_object_unref(bus);
                    break;
                }
                gst_object_unref(bus);
            }
            continue;
        }

        GstCaps *caps = gst_sample_get_caps(sample);
        GstBuffer *buffer = gst_sample_get_buffer(sample);

        if (caps == nullptr || buffer == nullptr) {
            gst_sample_unref(sample);
            continue;
        }

        GstVideoInfo vinfo;
        if (!gst_video_info_from_caps(&vinfo, caps)) {
            std::cerr << "[CameraCapture] gst_video_info_from_caps failed\n";
            gst_sample_unref(sample);
            continue;
        }

        GstVideoFrame vframe;
        if (!gst_video_frame_map(&vframe, &vinfo, buffer, GST_MAP_READ)) {
            std::cerr << "[CameraCapture] gst_video_frame_map failed\n";
            gst_sample_unref(sample);
            continue;
        }

        // Allocate an AVFrame to hand off to the rest of the pipeline.
        AVFrame *avf = av_frame_alloc();
        if (avf == nullptr) {
            gst_video_frame_unmap(&vframe);
            gst_sample_unref(sample);
            continue;
        }

        avf->format = AV_PIX_FMT_YUV420P;
        avf->width = GST_VIDEO_FRAME_WIDTH(&vframe);
        avf->height = GST_VIDEO_FRAME_HEIGHT(&vframe);

        if (av_frame_get_buffer(avf, 32) < 0) {
            std::cerr << "[CameraCapture] av_frame_get_buffer failed\n";
            av_frame_free(&avf);
            gst_video_frame_unmap(&vframe);
            gst_sample_unref(sample);
            continue;
        }

        // Copy the three YUV planes row-by-row, guarding against stride
        // mismatches.
        for (int plane = 0; plane < 3; ++plane) {
            const int src_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, plane);
            const int dst_stride = avf->linesize[plane];
            const int plane_height =
                (plane == 0) ? avf->height : (avf->height + 1) / 2;
            const int copy_width = std::min(src_stride, dst_stride);

            const auto *src = static_cast<const uint8_t *>(
                GST_VIDEO_FRAME_PLANE_DATA(&vframe, plane));
            uint8_t *dst = avf->data[plane];

            for (int row = 0; row < plane_height; ++row) {
                std::memcpy(dst + (static_cast<ptrdiff_t>(row) * dst_stride),
                            src + (static_cast<ptrdiff_t>(row) * src_stride),
                            static_cast<std::size_t>(copy_width));
            }
        }

        // PTS from the GStreamer buffer, in nanoseconds.
        GstClockTime buf_pts = GST_BUFFER_PTS(buffer);
        avf->pts = GST_CLOCK_TIME_IS_VALID(buf_pts)
                       ? static_cast<int64_t>(buf_pts)
                       : 0;

        gst_video_frame_unmap(&vframe);
        gst_sample_unref(sample);

        auto wrapped = std::make_unique<Frame>();
        wrapped->frame.reset(avf);
        wrapped->pts = avf->pts;
        wrapped->width = avf->width;
        wrapped->height = avf->height;
        wrapped->format = AV_PIX_FMT_YUV420P;

        if (frameCallback_) {
            frameCallback_(std::move(wrapped));
        }
    }

    running_.store(false, std::memory_order_relaxed);
}

} // namespace videoCore::capture
// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,
// bugprone-casting-through-void,
// cppcoreguidelines-pro-bounds-constant-array-index,
// clang-analyzer-optin.core.EnumCastOutOfRange,
// readability-implicit-bool-conversion)