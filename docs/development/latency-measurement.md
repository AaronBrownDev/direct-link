# Latency Measurement Methodology

## What is measured

Direct Link reports end-to-end latency from **camera frame capture to display swap**. The number shown in the director UI represents the full pipeline:

```
camera hardware → GStreamer callback → encode → WHIP → LiveKit ingress → SFU → jitter buffer → decode → QQuickWindow swap
```

This is broken into three components reported independently:

### 1. DC one-way (camera → director data channel arrival)

- The camera records `std::chrono::system_clock::now()` as `capture_ns` in the GStreamer preview callback (when the frame first reaches the application).
- That `capture_ns` is held in a `pts → capture_ns` map keyed by the frame's pipeline pts.
- When the encoder produces an output packet, `CameraSession`'s packet callback fires from the encode thread; `CameraSessionController` looks up the original `capture_ns` by the packet's pts (nearest-match within ~16 ms to tolerate encoder pts rescaling) and emits `frameCaptured`.
- `CameraLatencySender::onFrameCaptured` sends `capture_ns` over a LiveKit data channel (`reliable=true`).
- The director records `dc_arrived_ns = now()` when `DirectorTransport::onUserPacketReceived` fires.
- `dc_one_way_ms = (dc_arrived_ns + clock_offset_ns - capture_ns) / 1e6`.
- Pacing DC sends from the encoder output (rather than the camera capture rate) keeps DC and video frame arrival rates aligned at the receiver, which is required for accurate per-frame matching.
- Expected: ~1 ms on loopback, 5–50 ms over internet.

### 2. Video lag (data channel arrival → frame decoded)

- `video_lag_ms = (frame_received_ns - dc_arrived_ns) / 1e6`.
- Both timestamps are from the director's local clock; no clock offset is needed.
- Captures: WHIP WebRTC pipeline + LiveKit ingress buffering + SFU forwarding + **jitter buffer** + H.264 decode.
- **This is the dominant latency component**, ~1000 ms loopback, governed by the LiveKit Ingress jitter buffer floor. See the investigation section below.

### 3. Display gap (decoded frame available → screen swap)

- `QQuickWindow::frameSwapped` fires after Qt composites and swaps the frame to the display buffer.
- `DirectorTransport` measures the gap from frame decode to next `frameSwapped`, maintaining a 30-sample rolling average.
- In the headless E2E test this is always 0 ms. In the GUI it is typically 3–5 ms.

---

## Component matching

Each captured frame produces one DC packet (carrying its `capture_ns`) and one video frame (carrying libwebrtc's `timestamp_us` capture-time estimate, when populated). Both arrive at the director through different paths and may arrive at different rates if there is loss or rate adaptation. Naive FIFO matching breaks when those rates differ, so `DirectorTransport` uses a three-tier strategy:

1. **Timestamp-based match (preferred).** When `VideoFrameEvent::timestamp_us > 0` (libwebrtc populates this once an RTCP SR-based clock sync arrives — typically within the first few frames in a real deployment), match each frame to the DC entry whose `capture_ns` is closest to `frame.ts_us * 1000 − offset`. The constant offset between the two clock domains is seeded from the first valid frame and refined each subsequent frame via an EWMA (delta form, since the absolute offset is ~1.78×10¹⁸ ns and a naïve `prior * 7` would overflow `int64`). Tolerance: 100 ms. Match accuracy in practice: typically <30 ms `ts_match_diff`.

2. **Estimated-lag fallback.** When `ts_us` is never populated (observed on `network_mode: host` docker-compose where libwebrtc's clock-sync path doesn't fire), match each frame to the DC whose age (`received_ns − dc_arrived_ns`) is closest to a running estimate of `video_lag` (seeded at 1 s, refined via EWMA). Tolerance: 500 ms. Approximate but stable; suitable for local development.

3. **FIFO fallback.** Used only during the first few frames before either of the above can establish itself. Pops the oldest entry. Subject to the queue-cap artifact when DC and frame rates differ.

`m_capture_queue` is a `std::deque` capped at 60 entries (~2 s at 30 fps). Diagnostic logs annotate each frame with `[TS]`, `[FIFO]`, or a miss reason.

**Critical constraint:** `CameraSessionController::frameCaptured` must fire even when no `QVideoSink` is attached (headless mode). In `session_controller.cpp` the preview callback is always registered (it records `pts → capture_ns` regardless of sink); the `QVideoSink` render path is gated on `sink != nullptr` separately. Similarly, `VideoTrack::setTrack()` always calls `startRead()` regardless of whether a video sink is set — the read loop is required to emit `frameReceived` for latency measurement.

---

## Known issues and current latency investigation

### Observed measurements (loopback, local docker-compose, 2026-04-30)

| Component | Observed | Expected |
|---|---|---|
| dc_one_way | ~1 ms | ~1 ms (loopback) ✓ |
| video_lag | ~1005 ms | < 200 ms |
| display_gap | 0 ms (headless) / ~3 ms (GUI) | < 10 ms ✓ |
| **total** | **~1007 ms** | **< 250 ms** |

The entire remaining latency budget is consumed by `video_lag` — specifically the LiveKit Ingress jitter buffer floor. See ADR-0002 for the full investigation; the rest of this section captures the relevant points.

### Camera-side pipeline reduction (deployed, ~170 ms saved)

`dc_one_way` was previously ~174 ms because NVENC was buffering ~6 frames internally before producing first output (200 ms at 30 fps), even with `tune=ull`. Setting:

```c
av_dict_set(&options, "surfaces", "1", 0);
av_dict_set(&options, "delay", "0", 0);
av_dict_set(&options, "zerolatency", "1", 0);
```

drops the encode pipeline depth to 1–3 ms per frame.

### What was tried and what didn't help

| Attempt | Result |
|---|---|
| `gopSize = 30` (was 60) | Reduced ~1900 → ~1015 ms (combined with `do-timestamp`) |
| `gopSize = 3` (100 ms per GOP) | No change |
| `do-timestamp = TRUE` on appsrc | See above |
| `room.playout_delay: {enabled, min: 0, max: 200}` in livekit.yaml | Hint forwarded as RTP extension; livekit-ffi receiver ignores it |
| `MaxPlayoutDelay: 0` / `MinPlayoutDelay: 0` in director JWT `RoomConfiguration` | Same — ignored by livekit-ffi receiver |
| Reducing `appsink max-buffers` and `frameQueue` capacity | No effect — frames were not piling up there |
| `surfaces=1`, `delay=0`, `zerolatency=1` on NVENC | **Saved ~170 ms** in `dc_one_way` |

### Root cause of the residual ~1 s

The ~1000 ms is a fixed pipeline delay, not adaptive jitter:

- Consistent across ALL samples (not just startup)
- Independent of GOP size
- Independent of room/JWT playout-delay hints
- Confirmed on both loopback and GKE deployments

The livekit-ffi Rust SDK (`/home/abrown/livekit-cpp`) does NOT expose `set_jitter_buffer_minimum_delay` at the C++ API level. The function exists in `webrtc-sys/src/rtp_receiver.rs` but is not surfaced in the public `livekit::Room` / `livekit::VideoStream` API. The GStreamer `whipsink` is a Rust plugin (`gst-plugins-rs/webrtchttp`) that doesn't expose RTP header extension control through public properties, so the `playout-delay` extension can't be added without forking `whipsink`.

### Possible paths forward (not pursued)

1. **Patch livekit-ffi.** High-confidence fix; modifies vendored library files and creates a permanent fork that breaks on upstream updates. Requires distributing the patched build to every client deployment.

2. **Replace WHIP ingress with native LiveKit publish.** Tried — measured ~5900 ms `video_lag` due to libwebrtc's internal encoder buffering, plus loses the NVENC fast-path. Reverted.

3. **Add `playout-delay` RTP extension to GStreamer SDP.** Requires forking `whipsink` to expose extension negotiation; not exposed in current upstream version (0.14.4).

4. **Wait for upstream LiveKit Ingress to add a `JitterBufferMs` config.** Not present in protocol v1.45.

---

## What is not measured (known gaps)

### Camera hardware shutter lag (~5–30 ms, not corrected)

The timestamp is recorded in the GStreamer preview callback using `std::chrono::system_clock::now()`. This captures when the CPU receives the frame buffer from the V4L2 driver, not when the photons hit the sensor.

### Display photon gap (~1–4 ms, not corrected)

`QQuickWindow::frameSwapped` fires when the GPU swaps the back buffer. Pixel propagation through the display panel (response time, PWM) adds a further ~1–4 ms constant offset.

### Match degradation on persistent drops

The timestamp-based and estimated-lag matchers (see "Component matching" above) handle the common rate-mismatch case (e.g. encoder produces 30 fps but receiver decodes 10 fps). However, if a frame's matching DC packet is itself lost in transit, no match is possible and that frame's latency report is skipped (`ts-match MISS` in the diagnostic log). With `reliable=true` on the data channel, this is rare on a healthy network. The queue is capped at 60 entries (~2 s at 30 fps), which bounds how far back the matcher can look.

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
[Sample N/30]  dc=1ms  vid=1005ms  gap=0ms  total=1006ms
```

Statistics (min/mean/p50/p95/max) are printed at the end.

The headless test always uses the estimated-lag fallback because the test runs too briefly for libwebrtc's RTCP SR clock sync to populate `timestamp_us`. The GUI exercises the timestamp-based path on real deployments.

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
