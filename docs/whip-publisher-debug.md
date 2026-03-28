# WHIP Publisher Debug Session

This document explains a set of changes made to fix the operator camera feed not
appearing on the director's side. The investigation used a local `docker-compose`
stack to observe per-service logs and trace the full path from camera capture
through WHIP ingress to LiveKit room participant.

---

## What was broken

The operator's camera captured and encoded video. The `test_operator_session`
integration test returned exit 0 (WHIP handshake appeared to succeed), but the
director never saw the camera feed. The LiveKit room had no published track.

---

## Bug 1 — LiveKit ingress "missing API key or secret key"

**File:** `backend/configs/ingress.yaml`

### Symptom

Every WHIP connection failed immediately with:

```
ingress failed: "missing API key or secret key"
state: ENDPOINT_ERROR
```

The ingress service started and connected to Redis successfully, but every
incoming WHIP session was rejected before ICE even began.

### Root cause

`ingress.yaml` had empty `api_key` and `api_secret` fields with a comment
claiming they would be "overridden by env vars". The `docker-compose.prod.yaml`
set `LIVEKIT_API_KEY` and `LIVEKIT_API_SECRET` environment variables, but the
`livekit/ingress` Docker image does **not** map those env var names into the YAML
config fields. The ingress launched with no credentials and could not authenticate
with LiveKit when trying to publish an incoming stream.

### Fix

Set the dev credentials directly in `ingress.yaml`:

```yaml
api_key: "devkey"
api_secret: "dev-secret-that-is-32-chars-long"
ws_url: "ws://localhost:7880"
```

**Scope:** this file is only used by the local `docker-compose` stack. The
Kubernetes deployment uses `infrastructure/kubernetes/overlays/dev/patches/ingress-config.yaml`
as a ConfigMap overlay, which is mounted instead. That overlay sources its
credentials from ExternalSecrets and was already correct.

---

## Bug 2 — GStreamer whipsink pad request crash

**File:** `networking/src/whip_publisher.cpp`

### Symptom

```
GStreamer-CRITICAL: gst_pad_link_full: assertion 'GST_IS_PAD (sinkpad)' failed
GStreamer-CRITICAL: gst_object_unref: assertion 'object != NULL' failed
[CameraSession] Failed to initialize WHIP publisher
```

### Root cause

The original code requested a sink pad from `whipsink` using:

```cpp
GstPad *sink_pad = gst_element_request_pad_simple(whipsink, "sink_0");
```

`whipsink` is a thin wrapper around GStreamer's `webrtcbin`. `webrtcbin` uses a
dynamic `sink_%u` pad template that requires explicit RTP caps to be provided at
pad-request time — without them, the request returns `NULL`.

### Fix

Request the pad using `gst_element_request_pad` with a full `application/x-rtp`
caps structure describing the H.264 payload:

```cpp
GstCaps *rtp_caps = gst_caps_new_simple(
    "application/x-rtp",
    "media",         G_TYPE_STRING, "video",
    "encoding-name", G_TYPE_STRING, "H264",
    "payload",       G_TYPE_INT,    96,
    "clock-rate",    G_TYPE_INT,    90000,
    nullptr);
GstPadTemplate *templ = gst_element_class_get_pad_template(
    GST_ELEMENT_GET_CLASS(whipsink), "sink_%u");
GstPad *whip_sink_pad = gst_element_request_pad(whipsink, templ, nullptr, rtp_caps);
```

---

## Bug 3 — ICE failure due to missing STUN/TURN servers

**File:** `networking/src/whip_publisher.cpp`

### Symptom

The WHIP HTTP exchange succeeded (SDP offer/answer) but ICE either failed or
connected too slowly. No track was published to LiveKit.

### Root cause

`whipsink` was configured with only the `whip-endpoint` and `auth-token`. No
ICE server information was provided. LiveKit returns STUN/TURN server URLs in
`Link` headers of the WHIP POST response, but `whipsink` ignores those by
default. Without ICE servers, `webrtcbin` could only gather host candidates,
which may not be reachable on all networks.

### Fix

Enable `use-link-headers` so the client reads ICE servers from the WHIP
response, and add a STUN fallback:

```cpp
g_object_set(whipsink,
             "whip-endpoint", whipUrl_.c_str(),
             "auth-token",    streamKey_.c_str(),
             "use-link-headers", TRUE,
             "stun-server", "stun://stun.l.google.com:19302",
             nullptr);
```

---

## Bug 4 — H.264 profile rejected by WebRTC

**File:** `video-core/src/encode/software_encoder.cpp`

### Symptom

Even when ICE connected, the browser-side WebRTC stack could not decode the
stream. WebRTC mandates **Constrained Baseline Profile** for H.264 interop.

### Fix

```cpp
codecCtx_->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
```

---

## Bug 5 — WHIP publisher startup order race

**File:** `client/src/session/camera_session.cpp`

### Symptom

The WHIP handshake completed and ICE connected, but the LiveKit ingress still
timed out with `"source encoder not ready"`. The operator's participant never
joined the LiveKit room.

### Root cause

`camera_session.cpp` started the video pipeline (camera capture + encoder
thread) **before** calling `whipPublisher_.start()`. `start()` blocks for up to
20 seconds while the WHIP HTTP handshake and ICE negotiation complete.

During that entire window, `WHIPPublisher::isRunning()` returns `false`, so
every encoded packet pushed through the callback was silently dropped:

```cpp
void WHIPPublisher::pushPacket(...) {
    if (!isRunning()) return;   // ← drops every packet during handshake
    ...
}
```

After `running_ = true` was finally set, the encoder was already mid-GOP. The
next keyframe was not due for up to `gopSize` frames (default 60 frames at 30 fps
= 2 seconds). By the time the first keyframe arrived, the ingress had already
declared failure.

```
Original order:
  1. pipeline_.start()        → encoder thread starts, packets produced & dropped
  2. whipPublisher_.start()   → blocks ~7s for ICE; running_ = true
  3. (encoder must wait for next keyframe — up to 2s more)
```

### Fix

Swap the startup order so `running_ = true` is set before any frames are
produced:

```cpp
// Start WHIP publisher first: blocks until ICE/DTLS handshake completes
// and sets running_ = true before the encoder thread is started.
auto publisherResult = whipPublisher_.start();
if (publisherResult != networking::Result::Success) { ... }

// Now start the encoder — first packet is pushed immediately.
auto pipelineResult = pipeline_.start([this](auto pkt) {
    whipPublisher_.pushPacket(std::move(pkt));
});
```

---

## Bug 6 — GStreamer buffers held for 16 seconds due to absolute PTS

**File:** `networking/src/whip_publisher.cpp`, `networking/include/whip_publisher.hpp`

### Symptom

Even with the startup order fixed, the ingress still failed. Inspecting with
`GST_DEBUG=appsrc:5,h264parse:5` revealed:

```
appsrc: pop buffer, pts 0:00:16.266666667, dts 99:99:99.999999999
```

The first encoded packet arrived in `appsrc` at pipeline runtime ~3.8 seconds,
but its PTS was **16.26 seconds**. A further investigation with
`GST_DEBUG=dtls*:5` confirmed DTLS completed at 0:00:06.95 — fast. Yet the
first RTP data didn't reach `rtpsession` until 0:00:18, which matched exactly
the PTS value.

### Root cause

For a live GStreamer pipeline, `GST_BUFFER_PTS` must be **pipeline-relative**
(time since the pipeline base clock started). The PTS coming out of the encoder
was the raw v4l2 hardware timestamp: nanoseconds from when the camera device was
opened, which on this system was ~16 seconds. This is an absolute value, not
relative to the GStreamer pipeline.

GStreamer's `rtpsession` (inside `webrtcbin`) uses the PTS to schedule packet
transmission: it holds each buffer until the pipeline's running time reaches the
PTS. A PTS of 16 seconds on a pipeline that just started means every packet
sits in the queue for ~16 seconds before being forwarded — long after the
LiveKit ingress timeout.

The chain:
```
camera_capture.cpp  frame->pts = av_rescale_q(v4l2_ts, stream_tb, ns_tb)
                    → absolute nanoseconds from camera device open (~16s)

software_encoder.cpp  input_frame->pts = av_rescale_q(frame->pts, ns, enc_tb)
                      pkt->pts → rescaled back to ns → packet->pts ≈ 16s

whip_publisher.cpp  GST_BUFFER_PTS(buffer) = packet->pts  ← bug: absolute time
```

### Fix

Track the first packet's timestamp and subtract it to produce pipeline-relative
PTS values. First packet gets PTS = 0; each subsequent packet is offset from
stream start:

```cpp
// whip_publisher.hpp
std::int64_t streamStartPts_ = -1;

// whip_publisher.cpp — pushPacket()
if (streamStartPts_ < 0) {
    streamStartPts_ = packet->pts;
}
GstClockTime relativePts = static_cast<GstClockTime>(packet->pts - streamStartPts_);
GST_BUFFER_PTS(buffer) = relativePts;
```

`streamStartPts_` is reset to `-1` in `stop()` so the publisher can be reused.

### Result

After this fix, `track has started` in the ingress log appeared **immediately
after ICE connected** (< 100 ms), `ENDPOINT_PUBLISHING` followed within 1–2
seconds, and `mediaTrack published` confirmed the H.264 track was live in the
LiveKit room:

```
06:08:42  ICE connection state changed: connected
06:08:42  track has started  (video/H264, 1920×1080)
06:08:44  ingress state updated: ENDPOINT_PUBLISHING
06:08:44  mediaTrack published  trackID=TR_VCF63cSxpKQWuG
```

---

## Supporting changes

### `client/CMakeLists.txt`

- Made `LIVEKIT_DIR` a CMake cache variable (`CACHE PATH`) so it can be
  overridden with `-DLIVEKIT_DIR=<path>` without editing the file.
- Added the `test_operator_session` executable target (see below).
- Added `fmt` to `direct-link` link libraries (required at link time on Fedora).

### `networking/CMakeLists.txt`

- Added `pkg_check_modules(FFMPEG REQUIRED libavcodec libavutil)` and
  `${FFMPEG_INCLUDE_DIRS}` to `target_include_directories`.
  `whip_publisher.hpp` transitively includes `video-core/include/common/types.hpp`
  which pulls in `<libavcodec/packet.h>`. On Fedora, FFmpeg headers live under
  `/usr/include/ffmpeg/` and are not on the default include path.

### `WHIPPublisher` error reporting improvements

- Added `logBusError()` helper that drains GStreamer ERROR/WARNING messages from
  the bus and prints them to stderr — called on state change failure so the
  pipeline element responsible for the error is visible.
- Increased EOS drain timeout from 3 s to 10 s (`whipsink` sends an HTTP DELETE
  to the WHIP endpoint on EOS, which can take several seconds over a real
  network).
- Increased `gst_element_get_state` timeout from 5 s to 20 s. The `whipsink`
  default WHIP HTTP timeout is 15 s; the previous 5 s limit caused the publisher
  to give up before the handshake had a chance to complete.
- Changed EOS timeout from a hard error (invoking `onErrorCallback_`) to a
  graceful warning on stderr and forced `GST_STATE_NULL`. The timeout is expected
  when the WHIP server is unreachable or slow to acknowledge the DELETE.

### `client/tests/test_operator_session.cpp` (new file)

End-to-end integration test for the full operator flow:

1. Connect to signaling server
2. Create a session
3. Director join (establishes the LiveKit room)
4. Camera/operator join (creates WHIP ingress, gets URL + stream key)
5. `CameraSessionController::start(whipUrl, streamKey)` — 25 s timeout covering
   the full WHIP + ICE handshake
6. Stream for 10 seconds
7. `stop()` + close session

The server URL defaults to the cloud endpoint and can be overridden via `argv[1]`
for local `docker-compose` testing:

```bash
./test_operator_session http://localhost:50051
```

### `client/src/application/Main.qml`

- Updated default `channel` URL from `http://localhost:50051` to
  `http://34.174.71.83:50051` (cloud dev signaling server).

### `video-core/src/encode/software_encoder.cpp`

- Added `captureConfig.framerate = 30` (was 5; the v4l2 driver was silently
  overriding to 30 anyway, causing a `[v4l2] The driver changed the time per
  frame from 1/5 to 1/30` warning).
