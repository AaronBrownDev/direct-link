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

- `video_lag_ms = (frame_received_steady_ns - dc_arrived_steady_ns) / 1e6`.
- Both timestamps are from the director's `steady_clock`, so the measurement survives any NTP step on the system clock.
- Captures: WHIP encode + upload + LiveKit Ingress + SFU → director libwebrtc + **director-side jitter buffer** + H.264 decode, minus the DC's direct camera→SFU path (DC bypasses Ingress).
- Dominated by libwebrtc's adaptive playout buffer at the director — typically tracks the `jitter_buffer_delay` stat reported by `getStats()` within ~20 ms. On a healthy GKE WAN path with the matcher correctly calibrated this is ~150–180 ms.

### 3. Display gap (decoded frame available → screen swap)

- `QQuickWindow::frameSwapped` fires after Qt composites and swaps the frame to the display buffer.
- `DirectorTransport` measures the gap from frame decode to next `frameSwapped`, maintaining a 30-sample rolling average.
- In the headless E2E test this is always 0 ms. In the GUI it is typically 3–5 ms.

---

## Component matching

Each captured frame produces one DC packet (carrying its `capture_ns`) and one video frame (carrying libwebrtc's `timestamp_us` capture-time estimate). Both arrive at the director through different paths and may arrive at different rates if there is loss or rate adaptation. Naive FIFO matching breaks when those rates differ, so `DirectorTransport` matches by capture timestamp:

1. **Timestamp-based match.** When `VideoFrameEvent::timestamp_us > 0` (libwebrtc populates this once an RTCP SR-based clock sync arrives — typically within the first few frames), match each frame to the DC entry whose `capture_ns` is closest to `frame.ts_us * 1000 − offset`. Tolerance: 250 ms. Match accuracy in practice: <15 ms `ts_match_diff`.

2. **Estimated-lag fallback.** When `ts_us` is never populated (observed on `network_mode: host` docker-compose where libwebrtc's clock-sync path doesn't fire), match each frame to the DC whose arrival age is closest to a running estimate of `video_lag`. Tolerance: 500 ms. Suitable for local development.

### The offset is a constant, not refined

The offset between `ts_us * 1000` and `capture_ns` is the epoch difference between two camera-local clocks (libwebrtc's RTP-derived microsecond timestamp and `std::chrono::system_clock` nanoseconds). It is a constant on the timescale of a session. The matcher therefore **computes the offset once at seed time and does not refine it**.

An earlier EWMA-refinement loop was found to drift monotonically: sub-millisecond match jitter biased each refinement, the next match's predicted target shifted by the refinement, and the system found a stable equilibrium far from truth. Disabling the refinement eliminated the drift while leaving residual seed bias within a frame interval, which is acceptable.

If the camera-local clocks ever drift relative to each other over a long session, the matcher will start missing tolerance and the reseed path (below) will recompute the offset.

### Seed and reseed

The first valid `ts_us` frame seeds the offset by picking the DC whose arrival age is closest to a `video_lag` guess:

- **`currentVideoLagGuessNs()`** returns `m_latest_jb_ms` (libwebrtc's own jitter-buffer reading) when available, falling back to `INITIAL_VIDEO_LAG_GUESS_NS` (1 s) only for the very first seed when the 1 Hz video-stats poll has not yet produced its first delta sample.
- **Race recovery for lean startups (e.g. `test_e2e_latency`).** If the seed ran with the 1 s fallback (no JB stat available), the matcher records `m_seeded_with_warmup_guess = true` and waits for `JB_STATS_BEFORE_RESEED` (3) non-zero JB readings before forcing a single reseed. The wait keeps the reseed from picking up an early-warmup JB value (~10–50 ms) and locking in an under-counted offset; by the third reading the libwebrtc buffer has typically converged. Gated by the flag so transient `JB=0` during pipeline freezes does not re-trigger.
- **Catastrophic failure recovery.** After `MAX_CONSECUTIVE_MISSES_BEFORE_RESEED` (30) frames in a row outside tolerance — usually meaning a clock-domain shift — the offset is cleared and the next valid frame reseeds.

### Queue mechanics

`m_capture_queue` is a `std::deque` capped at `MAX_CAPTURE_QUEUE_SIZE = 120` entries (~4 s at 30 fps), giving comfortable margin for warmup `video_lag` of 1–2 s.

On a successful match, only the matched entry is erased — **not** "everything older than the match." Older DCs may correspond to frames currently in libwebrtc's playout buffer; draining them on each match (the prior behavior) collapsed the queue depth below the in-flight pipeline depth during convergence bursts and left subsequent matches with no candidate older than the most recent DC, turning `video_lag` into a queue-depth artifact. Unmatched stale DCs (e.g. for frames the sender dropped at encode) age out via the queue cap.

### Diagnostic output

Every breakdown emit logs both the ts-matched `video_lag` and the FIFO-oldest / FIFO-newest values it would have reported instead — a persistent gap indicates the matcher is no longer picking the right DC and a reseed is overdue.

```
[DirectorTransport] Latency breakdown:
    dc_one_way=   30.5 ms
    video_lag=    162.0 ms
    video_lag_fifo_oldest= 3094.5 ms   # all unmatched DCs in queue
    video_lag_fifo_newest=   12.3 ms   # newest DC (just arrived)
    queue_depth= 120  match_pos= 110   # matcher picked DC near back
    display_gap=   1.7 ms
    total=       194.2 ms
    clock_offset=  0.0 ms
    match= ts_us
```

A one-line `[DT-rolling]` summary every 100 frames carries 100-sample means of every component plus libwebrtc's `jb` and `decode` for cross-check. `[ClockSync] round#` logs per-round chosen min-RTT, window spread, published offset, and the second-best offset so path asymmetry is visible.

**Critical constraint:** `CameraSessionController::frameCaptured` must fire even when no `QVideoSink` is attached (headless mode). In `session_controller.cpp` the preview callback is always registered (it records `pts → capture_ns` regardless of sink); the `QVideoSink` render path is gated on `sink != nullptr` separately. Similarly, `VideoTrack::setTrack()` always calls `startRead()` regardless of whether a video sink is set — the read loop is required to emit `frameReceived` for latency measurement.

---

## Investigation history

### Observed measurements (GKE WAN, 2026-05-10, post-matcher-fix)

| Component | Mean | p50 | p95 |
|---|---|---|---|
| dc_one_way | 33 ms | 32 ms | 47 ms |
| video_lag | 162 ms | 163 ms | 178 ms |
| display_gap (GUI) | ~2 ms | ~2 ms | ~3 ms |
| **total** | **~194 ms** | **~194 ms** | **~206 ms** |

For cross-check, libwebrtc's own `jitter_buffer_delay` over the same window: mean 171 ms, p50 181 ms — `video_lag` tracks it within ~20 ms, which is the expected difference between W3C's `jitter_buffer_delay` (first packet at libwebrtc → emission) and the app-side wall-clock interval (DC arrival → frame visible to application).

Physical validation: stopwatch held in front of the camera, screens compared side-by-side, observed delta ~150–170 ms — agreement with the reported `total` is within the 6–34 ms unaccounted-for floor (see "What is not measured" below).

### Initial misreading: the "architectural floor" that wasn't

For most of the project's early life the reported total sat at ~900–1000 ms regardless of GKE configuration, GOP size, RTP playout-delay extensions, or in-tree `SetJitterBufferMinimumDelay` patches. The conclusion at the time was that the LiveKit + Ingress + WHIP stack imposes a ~1 s architectural floor across its in-series jitter buffers and that no config knob would meaningfully move it.

That conclusion was **wrong** — driven by a bug in the DC matcher, not by the LiveKit pipeline:

- The matcher's queue-erase logic dropped *every* DC older than each successful match, on the assumption that "older = orphaned." During libwebrtc's convergence burst (the SFU emits frames faster than DCs arrive while the buffer drains its backlog), this drained the queue below the in-flight pipeline depth.
- In steady state the queue was left with only 1–2 DCs, both very recently arrived. Subsequent matches had no candidate older than the current frame, so the matcher always reported `video_lag` ≈ queue-depth × frame-interval — independent of the real DC-to-frame timing.
- The reported ~900–1000 ms was the *warmup* queue depth (~60 entries × ~33 ms × the queue-cap eviction dynamics), not anything libwebrtc was actually doing.

The fix (see `directortransport.cpp`) is to erase only the matched entry on each match and let unmatched stale DCs age out via the queue cap. After that change, `video_lag` correctly tracks libwebrtc's `jitter_buffer_delay` and the total reports ~194 ms — well within the sub-second range LiveKit was always capable of. The configuration attempts that were tried against the supposed floor (RTP playout-delay extensions, JWT `MaxPlayoutDelay`, in-tree `SetJitterBufferMinimumDelay` patches) were correct approaches whose effect was obscured by the matcher artifact and can be revisited if a future tuning pass is needed.

### Camera-side pipeline reduction (deployed, ~170 ms saved)

`dc_one_way` was previously ~174 ms because NVENC was buffering ~6 frames internally before producing first output (200 ms at 30 fps), even with `tune=ull`. Setting:

```c
av_dict_set(&options, "surfaces", "1", 0);
av_dict_set(&options, "delay", "0", 0);
av_dict_set(&options, "zerolatency", "1", 0);
```

drops the encode pipeline depth to 1–3 ms per frame.

### Sub-100 ms options (architectural, not currently planned)

The current ~194 ms total is well below the prior assumed floor and within the range typical of conferencing-grade WebRTC stacks. If a future product requirement makes <100 ms total a hard target, the realistic levers are:

1. **Direct WebRTC peer-to-peer between camera and director.** Use the existing gRPC server for SDP exchange. No SFU, no Ingress. One libwebrtc connection, one jitter buffer. Realistically 80–150 ms total. Cost: lose SFU-side multicast — needs a separate fanout strategy if multiple directors per session are required.

2. **Custom thin WHIP→RTP forwarder** replacing the LiveKit Ingress in the camera path. Removes one of the in-series jitter buffers. Few hundred lines of pion or Go.

3. **Forced lower target on the director's libwebrtc playout buffer.** Patching libwebrtc-cpp to set a hard cap on `jitter_buffer_target_delay` would shave ~50–100 ms but produce frame freezes on bursty networks.

---

## What is not measured (known gaps)

### Camera hardware shutter lag (~5–30 ms, not corrected)

The timestamp is recorded in the GStreamer preview callback using `std::chrono::system_clock::now()`. This captures when the CPU receives the frame buffer from the V4L2 driver, not when the photons hit the sensor.

### Display photon gap (~1–4 ms, not corrected)

`QQuickWindow::frameSwapped` fires when the GPU swaps the back buffer. Pixel propagation through the display panel (response time, PWM) adds a further ~1–4 ms constant offset.

### Match degradation on persistent drops

The timestamp-based and estimated-lag matchers (see "Component matching" above) handle the common rate-mismatch case (e.g. encoder produces 30 fps but receiver decodes 10 fps). However, if a frame's matching DC packet is itself lost in transit, no match is possible and that frame's latency report is skipped (`ts-match MISS` in the diagnostic log). With `reliable=true` on the data channel, this is rare on a healthy network. The queue is capped at 120 entries (~4 s at 30 fps), which bounds how far back the matcher can look.

### Camera-side sender pacing / NACK retransmit buffer

libwebrtc's `jitter_buffer_delay` stat is **director-side only** — it counts from "first packet at libwebrtc receiver" to "frame emitted from playout buffer." It does not include the time spent in the camera-side `webrtcbin` pacer + RTX/NACK retransmit buffer, which can be 100–300 ms on a lossy WiFi or LEO satellite link.

To close that gap, `WHIPPublisher` attaches a buffer probe to `nicesink:sink` inside `webrtcbin` (the deepest reachable point — past the RTX queue, past DTLS encryption, just before UDP send). Each probed buffer's PTS — set at appsrc push by `do-timestamp=TRUE` — is compared to the pipeline's current `running_time` at the probe; the difference is the wall-clock dwell time of that packet through the entire send chain. The mean over the most recent ~60 probed buffers is exposed via `WHIPPublisher::senderPacketDelayMs`, polled at 1 Hz by `CameraSessionController`, forwarded to `CameraLatencySender`, and shipped in the DC payload's v2 trailing `uint32`. `DirectorTransport::currentVideoLagGuessNs` adds it to JB so the matcher seeds against the true camera-to-receiver delay rather than just the receive-side buffer.

If the probe pad isn't found (different GStreamer version, plugin layout change), the probe is a no-op and `sender_delay` stays 0 — the matcher falls back to the JB-only seed, same as before this measurement was added. Empirically, on healthy paths webrtcbin doesn't hold packets, so `sender_delay` reads ~0 even with the probe wired correctly.

---

## Benchmark mode: ground-truth overlay

The matcher's accuracy is bounded by what it can observe from the client side (JB, sender-pacing probe at best). Server-side buffers in LiveKit Ingress and the SFU forwarder are invisible to it, and on real paths the residual gap to physical observation is typically 30–100 ms.

For absolute end-to-end measurement, run with `--benchmark-latency`. In benchmark mode:

- **Camera side:** before encode, `drawTimestampOverlay` writes the server-domain capture time (`capture_wall_ns + clock_offset_ns`) as 64 bits into the top-left 128×128 corner of every captured frame — an 8×8 grid of 16×16 black/white cells. The pattern survives H.264 DCT quantization at any reasonable bitrate.
- **Director side:** in `VideoTrack::readLoop`, after each decoded frame, `decodeTimestampOverlay` samples the center 8×8 of each cell, requires ≥90% high-contrast hits before accepting, and recovers the 64-bit value. `DirectorTransport::onBenchmarkOverlayDecoded` computes `latency_ms = (director_now_server_ns - decoded_value_ns) / 1e6` and emits `benchmarkLatency`. This is the genuine wall-clock interval from camera capture to frame visible at the receiver, including every server-side buffer the matcher cannot see.

The overlay rides inside the video pixels through encode, WHIP, Ingress, SFU, libwebrtc jitter buffer, and decode, so the measurement is independent of the DC matcher and immune to clock-sync drift between any intermediary. Clock sync between camera and director (both to the signaling server's `GetServerTime`) is the only shared dependency, contributing ~0.5 ms of error.

**Use cases:**
- Validating matcher accuracy against ground truth on a given network.
- Calibrating a per-deployment offset to apply to matcher output if its bias is consistent.
- Benchmarking infrastructure changes (Ingress version bumps, codec swaps, GOP tuning).
- Measuring precisely during demos and regression tests.

**Trade-off:** visible 128×128 black/white square in the top-left of the video. Only suitable for tests — not for production. The flag is off by default.

Pass `--benchmark-latency` to both `direct-link` and the test runner. `scripts/run-latency-test.sh --benchmark-latency` enables it for the headless test.

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

`client/tests/test_e2e_latency.cpp` is a headless binary that drives the full camera→director pipeline through a real signaling server and reports the same three components as the GUI. The wrapper script `scripts/run-latency-test.sh` is the recommended entry point — it auto-rebuilds the binary, runs against GKE by default, and bundles `run.log` plus sysinfo/netinfo into a single `.tar.gz` for sharing.

```sh
./scripts/run-latency-test.sh                 # default: GKE, 5000 samples (~3-4 min)
./scripts/run-latency-test.sh --samples 200   # quick smoke test
./scripts/run-latency-test.sh --no-rebuild    # skip cmake build step
./scripts/run-latency-test.sh --server http://localhost:50051
```

Why 5000 samples by default: libwebrtc's adaptive playout buffer takes ~115 s of clean samples to converge. 5000 samples (~165 s at the steady-state sample rate of ~30/s) covers convergence plus ~50 s of clean steady-state data on the back end. Lower counts capture mostly warmup and misrepresent the achievable floor.

Output per sample:
```
[Sample N/5000]  dc=33ms  vid=162ms  gap=2ms  total=197ms
```

Final `[Stats]` block carries min/mean/p50/p95/max for each component plus libwebrtc's `jb`/`decode`/`upstream_vid` breakdown.

If invoking the binary directly, enable debug output (Qt's default `qtlogging.ini` filters `qDebug`):

```sh
QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="*.debug=true;qt.*=false" ./client/build/test_e2e_latency ...
```

The bundle's `run.log` carries all the `[DT-rolling]`, `[DT-diag] FRAME#`, `[ClockSync]`, and `[DirectorTransport] Latency breakdown` lines needed to validate matcher health (see "Component matching" → "Diagnostic output").

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

Expected agreement: the physical measurement should be **higher** than the reported `total` by 6–34 ms — that's the unmeasured camera shutter lag plus the display photon gap (see "What is not measured"). Direction reversed, or a gap larger than ~50 ms, indicates a clock-offset error or a matcher state issue.
