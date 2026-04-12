#include "../include/whip_publisher.hpp"
#include <cstring>
#include <gstreamer-1.0/gst/app/gstappsrc.h>
#include <gstreamer-1.0/gst/gst.h>
#include <gstreamer-1.0/gst/video/video.h>

namespace networking {
WHIPPublisher::~WHIPPublisher() {
    if (isRunning()) {
        stop();
    }
    if (pipeline_ != nullptr) {
        gst_object_unref(pipeline_);
    }
}

Result
WHIPPublisher::initialize(const std::string &whip_url,
                          const std::string &stream_key, int framerate,
                          std::function<void(std::string)> onErrorCallback) {
    whipUrl_ = whip_url;
    streamKey_ = stream_key;
    framerate_ = framerate;
    onErrorCallback_ = std::move(onErrorCallback);

    pipeline_ = gst_pipeline_new("whip-pipeline");
    if (pipeline_ == nullptr) {
        return Result::ErrorPipelineFailed;
    }

    appsrc_ = gst_element_factory_make("appsrc", "video-source");
    GstElement *h264parse =
        gst_element_factory_make("h264parse", "h264-parser");
    GstElement *rtph264pay =
        gst_element_factory_make("rtph264pay", "rtp-payload");
    GstElement *whipsink = gst_element_factory_make("whipsink", "whip-sink");

    if (!pipeline_ || !appsrc_ || !h264parse || !rtph264pay || !whipsink) {
        return Result::ErrorPipelineFailed;
    }

    // appsrc receives encoded H.264 byte-stream from videoCore
    GstCaps *caps = gst_caps_new_simple(
        "video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream",
        "alignment", G_TYPE_STRING, "au", nullptr);
    g_object_set(appsrc_, "caps", caps, "is-live", TRUE, "format",
                 GST_FORMAT_TIME, "block", FALSE, "max-bytes",
                 static_cast<guint64>(512 * 1024), // 512KB cap
                 "min-latency", static_cast<gint64>(0), "max-latency",
                 static_cast<gint64>(0), nullptr);
    gst_caps_unref(caps);

    g_object_set(rtph264pay, "config-interval", 1, nullptr);
    g_object_set(whipsink, "whip-endpoint", whipUrl_.c_str(), "auth-token",
                 streamKey_.c_str(), nullptr);

    gst_bin_add_many(GST_BIN(pipeline_), appsrc_, h264parse, rtph264pay,
                     whipsink, nullptr);

    if (!gst_element_link_many(appsrc_, h264parse, rtph264pay, nullptr)) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return Result::ErrorPipelineFailed;
    }

    GstPad *src_pad = gst_element_get_static_pad(rtph264pay, "src");
    GstPad *sink_pad = gst_element_request_pad_simple(whipsink, "sink_0");

    GstPadLinkReturn ret = gst_pad_link(src_pad, sink_pad);
    gst_object_unref(src_pad);
    gst_object_unref(sink_pad);

    if (ret != GST_PAD_LINK_OK) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return Result::ErrorPipelineFailed;
    }

    GstBus *bus = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(
        bus,
        [](GstBus * /*bus*/, GstMessage *msg, gpointer data) -> gboolean {
            auto *publisher = static_cast<WHIPPublisher *>(data);
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError *err = nullptr;
                gchar *debug_info = nullptr;
                gst_message_parse_error(msg, &err, &debug_info);
                publisher->onErrorCallback_("GStreamer Error: " +
                                            std::string(err->message));
                g_clear_error(&err);
                g_free(debug_info);
            }
            return TRUE;
        },
        this);
    gst_object_unref(bus);

    
    GstPad *appsrc_src = gst_element_get_static_pad(appsrc_, "src");
    gst_pad_add_probe(appsrc_src,
        GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
        [](GstPad *, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
            auto *self = static_cast<WHIPPublisher *>(data);
            GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
            if ((gst_video_event_is_force_key_unit(event) != 0) &&
                    self->forceKeyframeCallback_) {
                self->forceKeyframeCallback_();
            }
            return GST_PAD_PROBE_OK;
        },
        this, nullptr);
    gst_object_unref(appsrc_src);

    return Result::Success;
}

Result WHIPPublisher::start() {
    if (pipeline_ == nullptr) {
        return Result::ErrorNotInitialized;
    }

    if (isRunning()) {
        return Result::Success;
    }

    GstStateChangeReturn ret =
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        return Result::ErrorPipelineFailed;
    }

    // Wait up to 5 seconds for the async WHIP handshake to complete
    // This blocks the calling thread
    if (ret == GST_STATE_CHANGE_ASYNC) {
        GstState state;
        GstStateChangeReturn waited =
            gst_element_get_state(pipeline_, &state, nullptr, 5 * GST_SECOND);
        if (waited == GST_STATE_CHANGE_FAILURE) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            return Result::ErrorPipelineFailed;
        }
    }

    running_ = true;
    return Result::Success;
}

Result WHIPPublisher::stop() {
    if (pipeline_ == nullptr) {
        return Result::ErrorNotInitialized;
    }

    if (!isRunning()) {
        return Result::Success;
    }

    // Signal end of stream — flushes encoder and whipsink downstream
    // This is necessary to ensure all frames are sent and the stream is
    // properly closed on the server side Lock appsrc while sending EOS to
    // prevent race conditions
    {
        std::lock_guard<std::mutex> lock(appsrcMutex_);
        gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
    }

    // Wait for EOS to propagate through the pipeline, 3 second timeout
    GstBus *bus = gst_element_get_bus(pipeline_);
    GstMessage *msg =
        gst_bus_timed_pop_filtered(bus, 3 * GST_SECOND, GST_MESSAGE_EOS);
    gst_object_unref(bus);

    if (msg != nullptr) {
        gst_message_unref(msg);
    }
    else {
        if (onErrorCallback_) {
            onErrorCallback_(
                "Timeout waiting for EOS message from GStreamer pipeline");
        }
    }

    GstBus *bus2 = gst_element_get_bus(pipeline_);
    gst_bus_remove_watch(bus2);
    gst_object_unref(bus2);
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    appsrc_ = nullptr;
    running_ = false;
    return Result::Success;
}

void WHIPPublisher::pushPacket(std::unique_ptr<videoCore::Packet> packet) {
    if (!isRunning()) {
        return;
    }

    AVPacket *av = packet->packet.get();

    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, av->size, nullptr);

    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    std::memcpy(map.data, av->data, av->size);
    gst_buffer_unmap(buffer, &map);

    GST_BUFFER_PTS(buffer) = static_cast<GstClockTime>(packet->pts);
    if (framerate_ == 0) {
        framerate_ = 30; // Fallback to default if framerate is zero to avoid
                         // division by zero
    }
    GST_BUFFER_DURATION(buffer) =
        GST_SECOND / static_cast<GstClockTime>(framerate_);
    frameCount_++;

    {
        std::lock_guard<std::mutex> lock(appsrcMutex_);
        if (appsrc_ == nullptr) {
            gst_buffer_unref(buffer);
            return;
        }
        GstFlowReturn ret;
        g_signal_emit_by_name(appsrc_, "push-buffer", buffer, &ret);

        if (ret != GST_FLOW_OK) {
            if (onErrorCallback_) {
                onErrorCallback_("Failed to push buffer to GStreamer pipeline");
            }
        }
    }
}

} // namespace networking