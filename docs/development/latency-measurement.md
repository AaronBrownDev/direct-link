# Latency Measurement Methodology

## What is measured

Direct Link reports end-to-end latency from **camera frame capture to display swap**. The number shown in the director UI represents the full pipeline:

```
camera hardware → GStreamer callback → encode → network → decode → QQuickWindow swap
```

This is broken into two segments measured independently and summed:

### 1. Pipeline latency (camera → decoded frame available)

- The camera sends a frame capture timestamp (nanoseconds, server clock domain) via a LiveKit data channel immediately before encoding begins.
- The director records the local wall-clock time at the moment `VideoStream::read()` returns a decoded frame.
- Both timestamps are expressed in server clock domain using the NTP-derived clock offset computed by `ClockSync`. The difference is the pipeline latency.

### 2. Display gap (decoded frame available → screen swap)

- `QQuickWindow::frameSwapped` fires after Qt composites and swaps the frame to the display buffer.
- `DirectorTransport` measures the time from when each frame was decoded to when `frameSwapped` next fires, maintaining a 30-sample rolling average.
- This average is added to each pipeline latency sample before emitting `latencyMeasured`.

---

## What is not measured (known gaps)

### Camera hardware shutter lag (~5–30 ms, not corrected)

The timestamp is recorded in the GStreamer preview callback using `std::chrono::system_clock::now()`. This is the moment the CPU receives the frame buffer from the V4L2 driver — not the moment the photons hit the sensor. On most USB webcams this gap is 5–15 ms; on CSI cameras with hardware timestamps it could be corrected using V4L2 `VIDIOC_QUERYBUF` timestamps, but that requires platform-specific work.

### Display photon gap (~1–4 ms, not corrected)

`QQuickWindow::frameSwapped` fires when the GPU swaps the back buffer — before the pixels propagate through the display panel (response time and PWM). This is typically < 5 ms and constant for a given monitor.

### FIFO timestamp mismatch on frame drops

Camera timestamps and decoded video frames are matched in FIFO order. If a video frame is dropped by the encoder or network, the corresponding timestamp in the queue will be consumed by the *next* frame, reporting a latency that is one frame period too low (~33 ms at 30 fps). The queue is capped at 60 entries (~2 s at 30 fps); entries older than this are silently dropped.

---

## Clock synchronization

Both camera and director clocks are aligned to a server reference clock. On each data channel message exchange, `ClockSync` computes:

```
clock_offset_ns = server_time - local_time - (rtt_ns / 2)
```

The offset is stored as an atomic and applied when the camera sends timestamps and when the director computes latency. Clock drift is corrected on each `ClockSync` tick (every 5 s).

Typical NTP/SNTP accuracy over LAN is ±1–3 ms. Over the public internet it may be ±5–20 ms, which becomes a floor on latency accuracy.

---

## What this measures vs. industry approaches

| Method | What it captures | Accuracy | Used here |
|---|---|---|---|
| Physical reference clock (slow-motion phone) | True glass-to-glass (photons) | ±1 frame (~33 ms) | No |
| QR/visual overlay correlation | Encode+decode pipeline | ±1 frame, no clock sync needed | No |
| RTCP SR clock correlation | Network+jitter buffer only | ±5 ms | No |
| Data channel + decoded frame (this implementation) | Camera callback → display swap | ±3–25 ms depending on camera/display | **Yes** |

Most WebRTC vendors (Zoom, Google Meet) report latency via RTCP Sender Report correlation, which excludes camera and display latency. Direct Link's measurement includes both endpoints and is therefore a more complete (though not perfectly glass-to-glass) number.

---

## How to validate with a physical reference

To verify accuracy without specialized equipment:

1. Point the camera at a millisecond-precision stopwatch running on another screen.
2. Record the director display with a phone at 120+ fps slow motion.
3. For each slow-motion frame: note the stopwatch time visible in the camera feed and the stopwatch time visible on the director screen. The difference is true end-to-end latency.
4. Compare against the reported `latencyMeasured` value from the same period.

Expected agreement is within ±30 ms. Larger discrepancy indicates clock offset error or significant camera hardware lag not captured by the preview callback.
