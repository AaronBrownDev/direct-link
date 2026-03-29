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

---

## Bug 7 — Capture/encoder dimension mismatch → H.264 bitstream corruption

**Files:** `client/src/session/camera_session.cpp`, `video-core/src/encode/software_encoder.cpp`

### Symptom

Director saw a central black-and-white camera feed with four semi-transparent
green/magenta duplicates offset in a quadrant pattern (chromatic aberration /
RGB channel separation effect).  Director logs showed repeated:

```
[h264 @ 0x...] Frame num change from X to Y
```

### Root cause

`camera_session.cpp` hardcoded the encoder to 1920×1080 while the capture
config was 640×480.  `software_encoder.cpp::encodeFrame()` only triggered
`sws_scale` when `frame->format != AV_PIX_FMT_YUV420P`.  USB cameras decode
MJPEG to YUV420P, so the condition was never true — a 640×480 frame was fed
directly to a 1920×1080 `AVCodecContext`.  The encoder read beyond the frame
buffer boundaries, producing a corrupt bitstream with wrong stride arithmetic.

### Fix

1. Derive encoder dimensions from the capture config:
   ```cpp
   encoderConfig.width  = captureConfig.width;
   encoderConfig.height = captureConfig.height;
   ```
2. Also check dimension mismatch in the `sws_scale` trigger:
   ```cpp
   if (frame->format != AV_PIX_FMT_YUV420P ||
       frame->width  != codecCtx_->width    ||
       frame->height != codecCtx_->height   ||
       frame->linesize[0] == 0) { /* scale */ }
   ```
3. Changed capture to 1280×720 to match the 16:9 UI border.

---

## Bug 8 — v4l2 driver downgrades frame rate from 30 fps to 10 fps

**Files:** `video-core/include/capture/capture_config.hpp`,
`video-core/src/capture/camera_capture.cpp`,
`client/src/session/camera_session.cpp`

### Symptom

```
[video4linux2,v4l2 @ 0x...] The driver changed the time per frame from 1/30 to 1/10
```

Video was choppy — the encoder was configured for 30 fps but only receiving
10 fps from the camera.

### Root cause

v4l2 defaults to a raw pixel format (YUYV/NV12) when none is specified.  Most
USB cameras cannot sustain 30 fps at 1280×720 in raw format — the bandwidth
exceeds USB 2.0 limits — so the driver silently reduces the frame rate.

### Fix

Add a `pixelFormat` field to `CaptureConfig` and pass it as the `input_format`
v4l2 option when non-empty.  Set it to `"mjpeg"` in `camera_session.cpp`; MJPEG
is compressed and most cameras support 30 fps at 720p in this mode.

```cpp
// capture_config.hpp
std::string pixelFormat;  // e.g. "mjpeg", "yuyv422"

// camera_capture.cpp — setupDevice()
if (!config_.pixelFormat.empty()) {
    av_dict_set(&options, "input_format", config_.pixelFormat.c_str(), 0);
}

// camera_session.cpp
captureConfig.pixelFormat = "mjpeg";
```

---

## Bug 9 — swscaler warns about deprecated YUVJ pixel format

**File:** `video-core/src/encode/software_encoder.cpp`

### Symptom

```
[swscaler @ 0x...] deprecated pixel format used, make sure you did set range correctly
```

Repeated multiple times per frame.

### Root cause

The MJPEG decoder outputs `AV_PIX_FMT_YUVJ420P` — a deprecated alias for
`AV_PIX_FMT_YUV420P` with JPEG (full) color range.  Passing the deprecated
format directly to `sws_getContext` triggers the warning and silently skips the
full→limited range conversion, washing out colors in the encoded stream.

### Fix

Normalize deprecated `YUVJ*` formats before calling swscale and call
`sws_setColorspaceDetails` to rescale luma/chroma from full range (0–255) to
H.264 limited range (16–235 / 16–240):

```cpp
AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
bool srcFullRange = (frame->color_range == AVCOL_RANGE_JPEG);
switch (srcFmt) {
    case AV_PIX_FMT_YUVJ420P: srcFmt = AV_PIX_FMT_YUV420P; srcFullRange = true; break;
    case AV_PIX_FMT_YUVJ422P: srcFmt = AV_PIX_FMT_YUV422P; srcFullRange = true; break;
    case AV_PIX_FMT_YUVJ444P: srcFmt = AV_PIX_FMT_YUV444P; srcFullRange = true; break;
    default: break;
}
// ... sws_getContext with normalized srcFmt ...
if (srcFullRange) {
    sws_setColorspaceDetails(sws_ctx,
        sws_getCoefficients(SWS_CS_DEFAULT), 1,   // src: full range
        sws_getCoefficients(SWS_CS_DEFAULT), 0,   // dst: limited range
        0, 1 << 16, 1 << 16);
}
```

Also extend the conversion trigger to include `srcFullRange`:

```cpp
if (srcFmt != AV_PIX_FMT_YUV420P || srcFullRange || frame->width != ... )
```

---

## Bug 10 — Decoder cannot recover from RTP packet loss (frame_num change)

**Files:** `video-core/include/encode/encoder.hpp`,
`video-core/include/encode/software_encoder.hpp`,
`video-core/src/encode/software_encoder.cpp`,
`video-core/include/pipeline/video_pipeline.hpp`,
`video-core/src/pipeline/video_pipeline.cpp`,
`networking/include/whip_publisher.hpp`,
`networking/src/whip_publisher.cpp`,
`client/src/session/camera_session.cpp`

### Symptom

```
[h264 @ 0x...] Frame num change from 5 to 12
```

Video freezes for up to 1 second after any RTP packet loss event.

### Root cause

H.264 `frame_num` increments by 1 for each non-IDR frame.  When UDP packets
are dropped, the remote decoder observes a gap (e.g., 5 → 12) and sends an
RTCP PLI (Picture Loss Indication) or FIR (Full Intra Request) asking for a new
IDR frame.  The GStreamer `webrtcbin` (inside `whipsink`) translates the PLI/FIR
into a `GstForceKeyUnitEvent` travelling upstream.  Previously nothing handled
this event, so the decoder had to wait for the next scheduled GOP boundary
(up to `kGopSize / kFramerate` = 1 second).

### Fix

Three-layer change:

1. **Encoder** — add `requestKeyframe()` to the `Encoder` base class and
   implement it in `SoftwareEncoder` using an `std::atomic<bool> forceKeyframe_`
   flag.  In `encodeFrame()`, if the flag is set, force an IDR by setting
   `input_frame->pict_type = AV_PICTURE_TYPE_I` before `avcodec_send_frame`.

2. **Pipeline** — forward `requestKeyframe()` on `VideoPipeline` to the
   internal encoder.

3. **Publisher** — install a `GST_PAD_PROBE_TYPE_EVENT_UPSTREAM` probe on the
   `appsrc` src pad in `WHIPPublisher::initialize()`.  When
   `gst_video_event_is_force_key_unit()` returns true, invoke a registered
   `forceKeyframeCallback_`.  `CameraSession` wires this callback to
   `pipeline_.requestKeyframe()`.

```cpp
// camera_session.cpp
whipPublisher_.setKeyframeRequestCallback([this]() {
    pipeline_.requestKeyframe();
});
```

The decoder can now recover within a single frame interval (≤ 33 ms at 30 fps)
instead of up to 1 second.

---

## `test_stream_lifecycle` — end-to-end lifecycle test

**File:** `client/tests/test_stream_lifecycle.cpp`

Automated test that exercises the full operator → LiveKit → director pipeline
without manual interaction.  Uses the real camera rather than synthetic frames
to catch camera-specific issues (pixel format, frame rate, color range).

Requires:
- Docker stack running: `docker compose -f docker-compose.prod.yaml up -d`
- V4L2 camera at `/dev/video0` supporting MJPEG at 1280×720@30 fps

```bash
./build/test_stream_lifecycle [signaling_url]
# Default: http://localhost:50051
```

**Stats logged every 5 seconds:**
```
[lifecycle] --- t= 5 s ---  encoded= 150  keyframes= 5  received= 29  dropped= 0  publisherError= false
```

**Final report:**
```
[lifecycle] ===== Final Report =====
  Frames encoded  : 594 (expected ~ 600)
  Keyframes       : 20  (expected ~ 20)
  Frames dropped  : 0   (queue overflow)
  Frames received : 471
  Receive ratio   : 0.785 | < 0.5 suggests transport or ingress issues
  RESULT: PASS (frames flowing end-to-end)
```

Exit 0 = frames received end-to-end.  Exit 1 = no frames received.

---

## Known non-issues

### swscaler "deprecated pixel format" warnings

```
[swscaler @ 0x...] deprecated pixel format used, make sure you did set range correctly
```

These come from the **LiveKit C++ SDK's** internal `frame.convert(RGBA)` call,
not from the encoder or `software_encoder.cpp`.  The SDK uses swscale with
`YUVJ420P` as the source format regardless of the encoder's output color range.
They are cosmetic and do not affect correctness.  Adding `AVCOL_RANGE_MPEG` to
the encoder context (`software_encoder.cpp`) embeds the color range in the H.264
VUI for spec compliance but does not suppress the SDK-side warnings.

### Camera running at 10fps instead of 30fps

Some USB webcams only support MJPEG at 10fps at 1280×720 due to USB 2.0
bandwidth constraints.  The MJPEG fix (`input_format=mjpeg` in v4l2) is
correct — it eliminates the `[v4l2] driver changed the time per frame` warning —
but the camera's hardware limit is 10fps at this resolution.

Workaround: use a lower resolution (e.g. 640×480) or a camera with a USB 3.0
interface.  The test receive ratio is computed against actual encoded frames
(not a theoretical 30fps baseline) to give a meaningful transport quality metric
regardless of capture framerate.

### Camera PTS not advancing (v4l2 MJPEG)

The FFmpeg MJPEG decoder may output frames with non-advancing or stream-timebase
PTS values rather than nanoseconds.  `VideoPipeline::encodeLoop()` now assigns
sequential PTS values from a frame counter (`frameIndex × nsPerFrame`) instead
of forwarding camera timestamps.  This prevents GStreamer from receiving buffers
with identical timestamps, which previously caused a
`"GStreamer encountered a general resource error"` from `webrtcbin`.

---

## Ongoing issue — director display shows 4-quadrant magenta/green artifact

### Symptom

The director's camera feed displays the image as a 2×2 grid of identical
sub-images.  The top two quadrants have a strong magenta tint; the bottom two
have a green tint.  Flashing a bright light at the camera causes the top and
bottom halves to swap colors (top becomes green, bottom becomes magenta).

### What we've confirmed

**Diagnostic logging added to `test_stream_lifecycle` and `FrameReader`.**

The test's `QVideoSink::videoFrameChanged` callback and `FrameReader::pushFrame`
both show the incoming LiveKit frame is correctly formed at the Qt layer:

```
[FrameReader] incoming: type=0 dataSize=3686400 dims=1280x720
[FrameReader] stride OK: 5120
[lifecycle] Qt frame #1: Format_RGBA8888  bytesPerLine(0)=5120  mappedBytes(0)=3686400
```

- `type=0` → RGBA (SDK confirmed it)
- `dataSize=3686400` = `1280 × 720 × 4` (correct for packed RGBA)
- `bytesPerLine(0)=5120` = `1280 × 4` (no stride padding)
- Sampled pixel values: `R=G=B` everywhere sampled — correct grayscale, `A=255`
- PPM frame dump to `/tmp/frame_dump.ppm` confirmed no 4-quadrant structure in raw bytes
- Quadrant comparison: top-left vs top-right match rate ≈ 8% (random) — image is NOT tiled in memory

The raw `QVideoFrame` data is correct. The visual artifact is introduced downstream in
Qt's rendering pipeline (VideoOutput → QSGVideoNode → GPU texture).

### What we tried that did not fix it

1. **Switched from `VideoBufferType::RGBA` to `VideoBufferType::I420`** in
   `DirectorSession::attachTrack` and rewrote `FrameReader::pushFrame` to copy
   the three I420 planes into a `Format_YUV420P` QVideoFrame, bypassing the
   SDK-side I420→RGBA conversion entirely.

   Result: test passes (69% receive ratio, EXIT 0).  Visual artifact unchanged.
   The I420 path is committed and left in place as it is structurally cleaner,
   but it did not resolve the display issue.

### Leading theory

The bug is in Qt's rendering of `QVideoFrame` inside `VideoOutput` on this
platform.  Either:

- Qt's multimedia backend is re-encoding the frame into a GPU texture format
  that does not match the declared `QVideoFrameFormat`, OR
- The `VideoOutput` scenegraph node is applying wrong texture coordinates or
  an incorrect shader for the delivered pixel format.

The artifact (4 copies, top/bottom color inversion) is geometrically consistent
with the chroma (U/V) planes being placed at wrong vertical offsets in the
texture — which would be a backend-side I420/NV12 re-interpretation of the
frame even after we deliver it in the requested format.

### Where to go next

**Isolate whether the encoding/streaming pipeline is the source, or Qt's rendering.**

The best first step is to add a live camera preview on the **operator side**
before the video enters the WHIP pipeline.  This gives us a split view:

- The operator preview uses Qt Multimedia (`CameraDevice` / `QVideoSink`) to
  display the raw camera feed directly — no encoding, no streaming.
- The director feed goes through H.264 encode → WHIP → LiveKit → decode → display.

If both look the same (same 4-quadrant artifact), the problem is in Qt's
`VideoOutput` rendering on this machine, independent of streaming.

If only the director feed has the artifact, the problem is introduced somewhere
in the encode → stream → decode path.

**Operator preview implementation sketch:**

Add a `QCamera` + `QVideoSink` in `CameraSessionController` (or a new
`CameraPreview` component) that feeds the raw frames into the same
`FrameReader` / `VideoOutput` path as the director, but sourced directly from
the camera capture before encoding.  A separate `VideoOutput` in the
`SessionPage` for the camera role would display it alongside or instead of a
placeholder.
