#include "../include/whip_publisher.hpp"
#include <cstring>
#include <iostream>
#include <gstreamer-1.0/gst/app/gstappsrc.h>
#include <gstreamer-1.0/gst/gst.h>
#include <gstreamer-1.0/gst/video/video.h>

namespace networking {

namespace {
// Find the WHIP webrtcbin inside the outer whipsink wrapper.  webrtcbin is
// the top-level WebRTC element; nicesink (our probe target) lives inside
// its lazily-created transport_send_bin and is reached via the
// "deep-element-added" signal on this webrtcbin.  Caller owns the returned
// reference.
GstElement *findWebrtcbinInside(GstElement *pipeline) {
    if (pipeline == nullptr) { return nullptr; }
    GstElement *whipsink = gst_bin_get_by_name(GST_BIN(pipeline), "whip-sink");
    if (whipsink == nullptr) { return nullptr; }
    GstElement *webrtcbin = gst_bin_get_by_name(GST_BIN(whipsink), "whip-webrtcbin");
    gst_object_unref(whipsink);
    return webrtcbin;
}

// Returns true iff the element's factory matches the given factory name
// (e.g. "nicesink").  Compared by strstr to tolerate the gst-plugins-bad
// quirk of plugin-prefixed names ("nicesink", "tcpserversink", etc.).
bool elementFactoryNameContains(GstElement *element, const char *needle) {
    if (element == nullptr) { return false; }
    GstElementFactory *factory = gst_element_get_factory(element);
    if (factory == nullptr) { return false; }
    const gchar *fname = GST_OBJECT_NAME(GST_OBJECT_CAST(factory));
    return fname != nullptr && std::strstr(fname, needle) != nullptr;
}
} // namespace

WHIPPublisher::~WHIPPublisher() {
    if (isRunning()) {
        stop();
    }
    if (pipeline_ != nullptr) {
        gst_object_unref(pipeline_);
    }
}

GstPadProbeReturn WHIPPublisher::sendDelayProbe(GstPad * /*pad*/,
                                                GstPadProbeInfo *info,
                                                gpointer user_data) {
    auto *self = static_cast<WHIPPublisher *>(user_data);
    if (self == nullptr || self->pipeline_ == nullptr) {
        return GST_PAD_PROBE_OK;
    }
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer == nullptr) {
        return GST_PAD_PROBE_OK;
    }

    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    if (!GST_CLOCK_TIME_IS_VALID(pts)) {
        return GST_PAD_PROBE_OK;
    }

    // Compare the buffer's pipeline-running-time PTS (set at appsrc push
    // by do-timestamp=TRUE) to the pipeline's current running_time.  The
    // difference is the wall-clock dwell time of that buffer through
    // h264parse + rtph264pay + webrtcbin's pacer + RTX/NACK retransmit
    // buffer up to this probe point (post-pacer in rtpbin).
    GstClock *clk = gst_pipeline_get_clock(GST_PIPELINE(self->pipeline_));
    if (clk == nullptr) {
        return GST_PAD_PROBE_OK;
    }
    const GstClockTime now = gst_clock_get_time(clk);
    gst_object_unref(clk);
    const GstClockTime base = gst_element_get_base_time(GST_ELEMENT(self->pipeline_));
    if (now < base) {
        return GST_PAD_PROBE_OK;
    }
    const GstClockTime running_now = now - base;
    if (running_now < pts) {
        // Clock can rarely drift below PTS momentarily during slewing.
        return GST_PAD_PROBE_OK;
    }
    const std::uint64_t delay_ns = static_cast<std::uint64_t>(running_now - pts);

    // Rolling mean over SEND_DELAY_WINDOW samples.  When the window fills,
    // halve sum and count to maintain a slow-decay average instead of
    // dropping the window entirely — keeps the published value continuous
    // across the boundary.  No lock: this probe is invoked from the
    // streaming thread (a single producer); readers see a relaxed-atomic
    // snapshot of the published mean.
    self->delay_sum_ns_ += delay_ns;
    ++self->delay_count_;
    if (self->delay_count_ >= SEND_DELAY_WINDOW) {
        self->delay_sum_ns_ /= 2;
        self->delay_count_ /= 2;
    }
    if (self->delay_count_ > 0) {
        const double mean_ms =
            static_cast<double>(self->delay_sum_ns_) /
            (static_cast<double>(self->delay_count_) * 1'000'000.0);
        // Clamp identical to receive-side: > 500 ms or < 0 is treated as
        // a stats glitch and not propagated.
        const double clamped = (mean_ms < 0.0) ? 0.0
                             : (mean_ms > 500.0) ? 500.0
                             : mean_ms;
        self->last_send_delay_ms_.store(clamped, std::memory_order_relaxed);
    }
    return GST_PAD_PROBE_OK;
}

void WHIPPublisher::attachSendDelayProbe(GstPad *pad) {
    if (pad == nullptr || delay_probe_pad_ != nullptr) {
        return;
    }
    delay_probe_pad_ = GST_PAD(gst_object_ref(pad));
    delay_probe_id_ = gst_pad_add_probe(delay_probe_pad_,
                                        GST_PAD_PROBE_TYPE_BUFFER,
                                        &WHIPPublisher::sendDelayProbe, this, nullptr);
    GstElement *parent = gst_pad_get_parent_element(delay_probe_pad_);
    std::cerr << "[WHIPPublisher] send-delay probe attached at "
              << (parent ? GST_OBJECT_NAME(parent) : "?")
              << ":" << GST_PAD_NAME(delay_probe_pad_) << "\n";
    if (parent != nullptr) { gst_object_unref(parent); }
}

void WHIPPublisher::onDeepElementAdded(GstBin * /*bin*/, GstBin * /*subbin*/,
                                       GstElement *element, gpointer user_data) {
    auto *self = static_cast<WHIPPublisher *>(user_data);
    if (self == nullptr || element == nullptr) { return; }
    if (!elementFactoryNameContains(element, "nicesink")) {
        return; // not the element we want
    }
    GstPad *pad = gst_element_get_static_pad(element, "sink");
    if (pad != nullptr) {
        self->attachSendDelayProbe(pad);
        gst_object_unref(pad);
    }
}

void WHIPPublisher::setupSendDelayProbeListener() {
    if (pipeline_ == nullptr || delay_webrtcbin_ != nullptr) {
        return;
    }
    GstElement *webrtcbin = findWebrtcbinInside(pipeline_);
    if (webrtcbin == nullptr) {
        std::cerr << "[WHIPPublisher] send-delay probe: webrtcbin not found, "
                     "sender_delay will stay 0\n";
        return;
    }
    delay_webrtcbin_ = webrtcbin; // own this ref
    // deep-element-added fires whenever an element is added anywhere in the
    // webrtcbin tree (recursively into transport_send_bin etc.), which is
    // exactly when nicesink gets created post-DTLS-negotiation.
    delay_deep_added_id_ = g_signal_connect(webrtcbin, "deep-element-added",
                                            G_CALLBACK(&WHIPPublisher::onDeepElementAdded),
                                            this);
    // Eager check: if the pipeline raced past nicesink creation before we
    // hooked the signal, find it by recursive iteration.
    GstIterator *it = gst_bin_iterate_recurse(GST_BIN(webrtcbin));
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        auto *e = static_cast<GstElement *>(g_value_get_object(&v));
        if (elementFactoryNameContains(e, "nicesink")) {
            GstPad *pad = gst_element_get_static_pad(e, "sink");
            if (pad != nullptr) {
                attachSendDelayProbe(pad);
                gst_object_unref(pad);
            }
            g_value_reset(&v);
            break;
        }
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    std::cerr << "[WHIPPublisher] send-delay probe: listening for "
                 "nicesink under webrtcbin\n";
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
    // do-timestamp: GStreamer replaces each buffer's PTS with the pipeline's
    // running time at the moment the buffer is pushed.  Without this, PTS values
    // come from the CameraCapture pipeline's clock (a different GStreamer
    // pipeline), whose epoch differs from the publish pipeline's epoch by the
    // length of the WHIP ICE handshake (~1-3 s).  The cross-pipeline offset
    // makes rtph264pay emit RTP timestamps that appear seconds late to the
    // receiver, causing libwebrtc's adaptive jitter buffer to lock in at the
    // observed offset (~1900 ms).
    g_object_set(appsrc_, "caps", caps, "is-live", TRUE, "format",
                 GST_FORMAT_TIME, "block", FALSE, "max-bytes",
                 static_cast<guint64>(512 * 1024), // 512KB cap
                 "min-latency", static_cast<gint64>(0), "max-latency",
                 static_cast<gint64>(0), "do-timestamp", TRUE, nullptr);
    gst_caps_unref(caps);

    g_object_set(rtph264pay, "config-interval", 1, nullptr);

    // use-link-headers: LiveKit returns ICE servers in WHIP response Link
    // headers. Without this, webrtcbin has no STUN/TURN and ICE fails silently.
    // stun-server is a fallback if the server doesn't provide Link headers.
    g_object_set(whipsink,
                 "whip-endpoint", whipUrl_.c_str(),
                 "auth-token", streamKey_.c_str(),
                 "use-link-headers", TRUE,
                 "stun-server", "stun://stun.l.google.com:19302",
                 nullptr);

    gst_bin_add_many(GST_BIN(pipeline_), appsrc_, h264parse, rtph264pay,
                     whipsink, nullptr);

    // Link appsrc -> h264parse -> rtph264pay normally
    if (!gst_element_link_many(appsrc_, h264parse, rtph264pay, nullptr)) {
        std::cerr << "[WHIPPublisher] Failed to link appsrc -> h264parse -> rtph264pay\n";
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return Result::ErrorPipelineFailed;
    }

    // whipsink wraps webrtcbin internally; webrtcbin requires explicit caps
    // when requesting a sink pad — gst_element_link won't work without them.
    //
    // Two payload types are advertised:
    //   PT 96 — H.264 video, the actual encoded stream.
    //   PT 97 — RTX (RFC 4588), retransmissions for PT 96.
    //
    // Including the RTX cap is what flips on receiver-driven loss recovery:
    // webrtcbin's internal rtprtxsend buffers recently-sent H.264 packets,
    // and when the peer (LiveKit ingress) reports lost sequence numbers via
    // an RTCP NACK feedback, rtprtxsend re-emits those packets on PT 97.
    // The receiver re-orders them into the original stream with a single
    // RTT of extra delay instead of stalling on a missing fragment until
    // the next IDR/PLI cycle.  See docs/development/webrtc-packet-recovery.md
    // for the full picture.
    GstCaps *rtp_caps = gst_caps_new_empty();
    gst_caps_append_structure(rtp_caps,
        gst_structure_new("application/x-rtp",
            "media",         G_TYPE_STRING, "video",
            "encoding-name", G_TYPE_STRING, "H264",
            "payload",       G_TYPE_INT,    96,
            "clock-rate",    G_TYPE_INT,    90000,
            nullptr));
    gst_caps_append_structure(rtp_caps,
        gst_structure_new("application/x-rtp",
            "media",         G_TYPE_STRING, "video",
            "encoding-name", G_TYPE_STRING, "rtx",
            "payload",       G_TYPE_INT,    97,
            "clock-rate",    G_TYPE_INT,    90000,
            "apt",           G_TYPE_INT,    96,
            nullptr));
    GstPadTemplate *templ = gst_element_class_get_pad_template(
        GST_ELEMENT_GET_CLASS(whipsink), "sink_%u");
    GstPad *whip_sink_pad =
        gst_element_request_pad(whipsink, templ, nullptr, rtp_caps);
    gst_caps_unref(rtp_caps);

    if (whip_sink_pad == nullptr) {
        std::cerr << "[WHIPPublisher] Failed to request pad from whipsink\n";
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return Result::ErrorPipelineFailed;
    }

    GstPad *pay_src_pad = gst_element_get_static_pad(rtph264pay, "src");
    GstPadLinkReturn link_ret = gst_pad_link(pay_src_pad, whip_sink_pad);
    gst_object_unref(pay_src_pad);
    gst_object_unref(whip_sink_pad);

    if (link_ret != GST_PAD_LINK_OK) {
        std::cerr << "[WHIPPublisher] Failed to link rtph264pay -> whipsink: "
                  << link_ret << "\n";
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return Result::ErrorPipelineFailed;
    }

    // Reach into whipsink's internal webrtcbin to flip on transceiver-level
    // NACK handling.  Modern GStreamer (≥ 1.22) exposes do-nack on each
    // GstWebRTCRTPTransceiver — when set on a sendonly transceiver, the
    // webrtcbin element honours incoming RTCP NACK by retransmitting via
    // the RTX payload type negotiated above.  Without this, NACK feedback
    // received from the peer would be ignored even though our SDP advertises
    // both `nack` and the rtx codec, leaving us with PLI-only recovery
    // (i.e. wait for the next IDR).  See docs/development/webrtc-packet-recovery.md.
    GstElement *webrtcbin =
        gst_bin_get_by_name(GST_BIN(whipsink), "whip-webrtcbin");
    if (webrtcbin != nullptr) {
        GArray *transceivers = nullptr;
        g_signal_emit_by_name(webrtcbin, "get-transceivers", &transceivers);
        if (transceivers != nullptr) {
            for (guint i = 0; i < transceivers->len; ++i) {
                GObject *t =
                    g_array_index(transceivers, GObject *, i);
                if (t == nullptr) {
                    continue;
                }
                if (g_object_class_find_property(
                        G_OBJECT_GET_CLASS(t), "do-nack") != nullptr) {
                    g_object_set(t, "do-nack", TRUE, nullptr);
                }
            }
            g_array_unref(transceivers);
        }
        gst_object_unref(webrtcbin);
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

    // Intercept GstForceKeyUnitEvent travelling upstream from webrtcbin (inside
    // whipsink) when the remote decoder sends an RTCP PLI or FIR.  Calling
    // forceKeyframeCallback_ asks the encoder to emit an IDR on the next frame
    // so the decoder can recover immediately rather than waiting for the next
    // scheduled GOP boundary.
    GstPad *appsrc_src = gst_element_get_static_pad(appsrc_, "src");
    gst_pad_add_probe(appsrc_src,
        GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
        [](GstPad *, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
            auto *self = static_cast<WHIPPublisher *>(data);
            GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
            if (gst_video_event_is_force_key_unit(event) &&
                    self->forceKeyframeCallback_) {
                self->forceKeyframeCallback_();
            }
            return GST_PAD_PROBE_OK;
        },
        this, nullptr);
    gst_object_unref(appsrc_src);

    return Result::Success;
}

void WHIPPublisher::logBusError() {
    if (pipeline_ == nullptr) {
        return;
    }
    GstBus *bus = gst_element_get_bus(pipeline_);
    GstMessage *msg =
        gst_bus_timed_pop_filtered(bus, 0, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));
        
    while (msg != nullptr) {
        GError *err = nullptr;
        gchar *debug_info = nullptr;
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            gst_message_parse_error(msg, &err, &debug_info);
            std::cerr << "[WHIPPublisher] GStreamer error: " << err->message << "\n";
        } else {
            gst_message_parse_warning(msg, &err, &debug_info);
            std::cerr << "[WHIPPublisher] GStreamer warning: " << err->message << "\n";
        }
        if (debug_info) {
            std::cerr << "[WHIPPublisher] Debug: " << debug_info << "\n";
        }
        g_clear_error(&err);
        g_free(debug_info);
        gst_message_unref(msg);
        msg = gst_bus_timed_pop_filtered(bus, 0,
                                         static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));
    }
    gst_object_unref(bus);
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
        logBusError();
        return Result::ErrorPipelineFailed;
    }

    // Wait up to 20 seconds — whipsink default WHIP timeout is 15s
    if (ret == GST_STATE_CHANGE_ASYNC) {
        GstState state;
        GstStateChangeReturn waited =
            gst_element_get_state(pipeline_, &state, nullptr, 20 * GST_SECOND);

        if (waited == GST_STATE_CHANGE_FAILURE) {
            logBusError();
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            return Result::ErrorPipelineFailed;
        }
        if (waited == GST_STATE_CHANGE_ASYNC) {
            std::cerr << "[WHIPPublisher] Timed out waiting for WHIP handshake\n";
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            return Result::ErrorPipelineFailed;
        }
    }

    // The pipeline clock has been running since gst_element_set_state was
    // called, which includes the ICE + DTLS handshake time (up to ~2 s on
    // real networks).  With do-timestamp=TRUE on appsrc, the first encoded
    // frame would receive a PTS equal to that elapsed time, causing
    // libwebrtc's jitter buffer at the receiver to lock in that duration as
    // its permanent playout offset.
    //
    // Fix: reset the appsrc base_time to "right now" so that the first
    // pushed buffer gets PTS ≈ 0 (one frame period ~33 ms) regardless of
    // how long ICE took.  No RTP data has been sent yet at this point, so
    // the receiver jitter buffer has no prior state to invalidate.
    {
        GstClock *clk = gst_pipeline_get_clock(GST_PIPELINE(pipeline_));
        if (clk != nullptr) {
            const GstClockTime now = gst_clock_get_time(clk);
            gst_element_set_base_time(GST_ELEMENT(pipeline_), now);
            gst_element_set_base_time(GST_ELEMENT(appsrc_), now);
            gst_object_unref(clk);
        }
    }

    running_ = true;
    // Hook rtpbin's pad-added so we attach the buffer probe the moment
    // send_rtp_src_<n> is created — which is after DTLS negotiation
    // completes, several seconds into the session on a real network.
    setupSendDelayProbeListener();
    return Result::Success;
}

Result WHIPPublisher::stop() {
    if (pipeline_ == nullptr) {
        return Result::ErrorNotInitialized;
    }

    if (!isRunning()) {
        return Result::Success;
    }

    // Detach the send-delay probe and disconnect the pad-added signal
    // before tearing down the pipeline so a late callback can't see a
    // half-destroyed pipeline_.  gst_pad_remove_probe is synchronous
    // w.r.t. the streaming thread: any probe currently in flight is
    // allowed to complete, then the source is removed.
    if (delay_probe_pad_ != nullptr) {
        if (delay_probe_id_ != 0) {
            gst_pad_remove_probe(delay_probe_pad_, delay_probe_id_);
            delay_probe_id_ = 0;
        }
        gst_object_unref(delay_probe_pad_);
        delay_probe_pad_ = nullptr;
    }
    if (delay_webrtcbin_ != nullptr) {
        if (delay_deep_added_id_ != 0) {
            g_signal_handler_disconnect(delay_webrtcbin_, delay_deep_added_id_);
            delay_deep_added_id_ = 0;
        }
        gst_object_unref(delay_webrtcbin_);
        delay_webrtcbin_ = nullptr;
    }

    // Signal end of stream — flushes encoder and whipsink downstream
    // This is necessary to ensure all frames are sent and the stream is
    // properly closed on the server side Lock appsrc while sending EOS to
    // prevent race conditions
    {
        std::lock_guard<std::mutex> lock(appsrcMutex_);
        gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
    }

    // Wait for EOS to propagate — whipsink sends an HTTP DELETE to the WHIP
    // server on EOS, which can take a few seconds on a remote endpoint.
    GstBus *bus = gst_element_get_bus(pipeline_);
    GstMessage *msg =
        gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND, GST_MESSAGE_EOS);
    gst_object_unref(bus);

    if (msg != nullptr) {
        gst_message_unref(msg);
    }
    else {
        std::cerr << "[WHIPPublisher] EOS drain timed out; forcing pipeline to NULL\n";
    }

    GstBus *bus2 = gst_element_get_bus(pipeline_);
    gst_bus_remove_watch(bus2);
    gst_object_unref(bus2);
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    appsrc_ = nullptr;
    running_ = false;
    streamStartPts_ = -1;
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

    // Normalize to pipeline-relative time.  v4l2 timestamps are absolute
    // (from device open), not relative to the GStreamer pipeline base time.
    // Passing the raw PTS would cause GStreamer to hold every buffer until
    // the pipeline clock reaches that timestamp, introducing a ~16-second
    // scheduling delay before any data reaches the ingress.
    if (streamStartPts_ < 0) {
        streamStartPts_ = packet->pts;
    }
    GstClockTime relativePts =
        static_cast<GstClockTime>(packet->pts - streamStartPts_);
    GST_BUFFER_PTS(buffer) = relativePts;
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