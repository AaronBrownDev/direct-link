# Latency Measurement Methodology

## What is measured

Direct Link reports end-to-end latency from **camera frame capture to display swap**. The number shown in the director UI represents the full pipeline:

```
camera hardware → GStreamer callback → encode → WHIP → LiveKit ingress → SFU → jitter buffer → decode → QQuickWindow swap
```

This is broken into three components reported independently:

### 1. DC one-way (camera → director data channel arrival)

- The camera emits `CameraSessionController::frameCaptured` in the GStreamer preview callback, recording `std::chrono::system_clock::now()` as `capture_ns`.
- `CameraLatencySender::onFrameCaptured` sends this timestamp over a LiveKit data channel (unreliable).
- The director records `dc_arrived_ns = now()` when `DirectorTransport::onUserPacketReceived` fires.
- `dc_one_way_ms = (dc_arrived_ns + clock_offset_ns - capture_ns) / 1e6`
- Expected: 1–5 ms on LAN, 5–50 ms over internet. On loopback: ~1 ms.

### 2. Video lag (data channel arrival → frame decoded)

- `video_lag_ms = (frame_received_ns - dc_arrived_ns) / 1e6`
- Both timestamps are from the director's local clock; no clock offset is needed.
- Captures: GStreamer encode delay + WHIP WebRTC pipeline + LiveKit ingress buffering + SFU forwarding + **jitter buffer** + H.264 decode.
- **This is the dominant latency component.** On loopback with current stack it measures ~1000–1035 ms. See the investigation section below.

### 3. Display gap (decoded frame available → screen swap)

- `QQuickWindow::frameSwapped` fires after Qt composites and swaps the frame to the display buffer.
- `DirectorTransport` measures the gap from frame decode to next `frameSwapped`, maintaining a 30-sample rolling average.
- In the headless E2E test this is always 0 ms. In the GUI it is typically 3–5 ms.

---

## Component matching

Camera timestamps and decoded video frames are matched in FIFO order by `DirectorTransport`:

- `CameraLatencySender::onFrameCaptured` sends one DC message per captured frame.
- `DirectorTransport::onUserPacketReceived` pushes `{capture_ns, dc_arrived_ns}` to `m_capture_queue` (capped at 60 entries).
- `DirectorTransport::onFrameArrived` (called by `VideoTrack::readLoop` each time `VideoStream::read()` returns) pops the oldest entry and computes the breakdown.

**Critical constraint:** `CameraSessionController::frameCaptured` must fire even when no `QVideoSink` is attached (headless mode). In `session_controller.cpp` the preview callback is always registered; the `QVideoSink` render path is gated on `sink != nullptr` separately. Similarly, `VideoTrack::setTrack()` always calls `startRead()` regardless of whether a video sink is set — the read loop is required to emit `frameReceived` for latency measurement.

---

## Known issues and current latency investigation

### Observed measurements (loopback, local docker-compose, 2026-04-29)

| Component | Observed | Expected |
|---|---|---|
| dc_one_way | ~1 ms | ~1 ms (loopback) ✓ |
| video_lag | ~1010–1035 ms | < 200 ms |
| display_gap | 0 ms (headless) / ~3 ms (GUI) | < 10 ms ✓ |
| **total** | **~1015–1040 ms** | **< 250 ms** |

The entire latency budget is consumed by `video_lag`.

### What was tried and what didn't help

| Attempt | Result |
|---|---|
| `gopSize = 30` (was 60) | Reduced from ~1900 ms to ~1015 ms |
| `gopSize = 3` (100 ms per GOP) | No change from gopSize=30 — still ~1015 ms |
| `do-timestamp = TRUE` on appsrc | Reduces from ~1900 ms to ~1015 ms (combined with gopSize change; individual effect unclear) |
| `room.playout_delay: {enabled: true, min: 0, max: 200}` in livekit.yaml | No measurable effect |
| `MaxPlayoutDelay: 0` in director JWT `RoomConfiguration` | No measurable effect |
| LiveKit server restart (to reload config) | Confirmed config loaded; no latency change |

### Root cause hypothesis

The ~1000 ms is a fixed pipeline delay, not adaptive jitter. Evidence:

- It is consistent across ALL samples (not just startup)
- It is independent of GOP size (gopSize=3 gives the same ~1000 ms as gopSize=30)
- Both playout_delay hints (room-level config and per-participant JWT) are ignored

The livekit-ffi Rust SDK (`/home/abrown/livekit-cpp`) does NOT expose `set_jitter_buffer_minimum_delay` at the C++ API level. The function exists in `webrtc-sys/include/livekit/rtp_receiver.h` and `webrtc-sys/src/rtp_receiver.rs` but is not surfaced in the public `livekit::Room` / `livekit::VideoStream` API.

The delay is most likely one of:
1. **LiveKit ingress WebRTC jitter buffer** (~300 ms startup buffer in `ioservice_ingress.go`) plus re-buffering at the ingress→SFU WebRTC connection.
2. **Director subscriber jitter buffer** locked at ~1000 ms because the playout_delay RTP extension is sent by the server but not processed by the livekit-ffi subscriber.

The GStreamer `whipsink` SDP offer does not include a `playout-delay` RTP extension attribute. The LiveKit ingress likely uses its own default jitter buffer settings regardless of the `room.playout_delay` config.

### Possible paths forward

1. **Patch livekit-ffi to call `set_jitter_buffer_minimum_delay(0)` on video RTP receivers.** This would be done in the Rust layer when a video track is subscribed. Requires rebuilding `/home/abrown/livekit-cpp`. High confidence this fixes the issue.

2. **Switch from WHIP ingress to direct LiveKit participant publish.** Eliminates the ingress pipeline entirely; the camera would publish as a native LiveKit participant. Requires replacing `WHIPPublisher` with a livekit-ffi-based video source. Major architectural change.

3. **Add `playout-delay` RTP header extension negotiation to `whipsink` pipeline.** The GStreamer `webrtcbin` (used internally by whipsink) supports RTP extensions. Adding `extmap:N http://www.webrtc.org/experiments/rtp-hdrext/playout-delay` to the SDP and sending the extension with `min=0, max=0` might cause the ingress to reduce its jitter buffer. Requires GStreamer pipeline changes.

4. **Instrument the ingress with `GST_DEBUG`** to measure actual RTP timestamp jitter on the ingress→SFU connection and confirm which jitter buffer is the source.

---

## What is not measured (known gaps)

### Camera hardware shutter lag (~5–30 ms, not corrected)

The timestamp is recorded in the GStreamer preview callback using `std::chrono::system_clock::now()`. This captures when the CPU receives the frame buffer from the V4L2 driver, not when the photons hit the sensor.

### Display photon gap (~1–4 ms, not corrected)

`QQuickWindow::frameSwapped` fires when the GPU swaps the back buffer. Pixel propagation through the display panel (response time, PWM) adds a further ~1–4 ms constant offset.

### FIFO timestamp mismatch on frame drops

If a video frame is dropped by the encoder or network, its DC timestamp is consumed by the *next* decoded frame, reporting latency that is one frame period (~33 ms) too low. The queue is capped at 60 entries (~2 s at 30 fps).

---

## Clock synchronization

Both camera and director clocks are aligned to a server reference clock. On each data channel exchange, `ClockSync` computes:

```
clock_offset_ns = server_time - local_time - (rtt_ns / 2)
```

The offset is stored as an atomic and applied when the camera sends timestamps and when the director computes `dc_one_way`. Clock drift is corrected on each `ClockSync` tick (every 5 s). On loopback the offset is ~0.06 ms.

Typical NTP/SNTP accuracy over LAN is ±1–3 ms. Over the public internet it may be ±5–20 ms, which becomes the floor on `dc_one_way` accuracy.

---

## E2E latency test

`client/tests/test_e2e_latency.cpp` provides a headless binary for measuring all three components against a real camera and local docker-compose stack:

```sh
# Start docker-compose.prod.yaml first
./client/build/test_e2e_latency --server http://localhost:50051 --samples 30
```

Output per sample:
```
[Sample N/30]  dc=1ms  vid=1015ms  gap=0ms  total=1016ms
```

Statistics (min/mean/p50/p95/max) are printed at the end.

To enable debug output (Qt debug messages are filtered by `/usr/share/qt6/qtlogging.ini` by default):
```sh
QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="*.debug=true;qt.*=false" ./client/build/test_e2e_latency ...
```

---

## What this measures vs. industry approaches

| Method | What it captures | Accuracy | Used here |
|---|---|---|---|
| Physical reference clock (slow-motion phone) | True glass-to-glass (photons) | ±1 frame (~33 ms) | No |
| QR/visual overlay correlation | Encode+decode pipeline | ±1 frame, no clock sync needed | No |
| RTCP SR clock correlation | Network+jitter buffer only | ±5 ms | No |
| Data channel + decoded frame (this implementation) | Camera callback → display swap | ±3–25 ms depending on camera/display | **Yes** |

---

## How to validate with a physical reference

1. Point the camera at a millisecond-precision stopwatch running on another screen.
2. Record the director display with a phone at 120+ fps slow motion.
3. For each slow-motion frame: note the stopwatch time in the camera feed and on the director screen. The difference is true glass-to-glass latency.
4. Compare against the reported `total` value from `test_e2e_latency` for the same period.

Expected agreement is within ±30 ms. Larger discrepancy indicates clock offset error or significant camera hardware lag.
