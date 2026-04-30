# 0002. End-to-End Camera-to-Display Latency Investigation

## Status

Accepted — measurement methodology and reachable floor established. Further reduction blocked on a livekit-ffi patch (out of scope for MVP).

## Context

### Problem

DirectLink measures end-to-end camera-to-display latency as the sum of three components:

| Component | Definition | Current |
|-----------|-----------|---------|
| `dc_one_way_ms` | Camera capture timestamp → director data-channel receive (one-way, clock-offset-corrected) | ~1 ms |
| `video_lag_ms` | Director data-channel receive → director video frame render (`frame_received_ns - dc_arrived_ns`) | ~1005 ms |
| `display_gap_ms` | Video frame render → actual screen display (GPU pipeline) | ~3–10 ms |

`video_lag_ms` is the LiveKit Ingress jitter buffer floor. It is reachable now from both loopback and GKE, after camera-side encoder pipeline and measurement methodology fixes described below.

Initial state of this investigation: `video_lag_ms` reported ~1900 ms on real network and ~1015 ms on loopback, with `dc_one_way_ms` reporting ~174 ms.

### Architecture

```
Camera (Linux)                   LiveKit GKE
  v4l2 → NVENC (H.264)
  → GStreamer appsrc
  → GStreamer whipsink (WHIP)  →  LiveKit Ingress  →  LiveKit SFU
                                                          ↓
                                               Director (livekit-cpp SDK)
                                                  ← VideoStream frames
```

The camera uses NVENC for hardware H.264 encoding. WHIP delivers the RTP stream to the LiveKit Ingress, which re-packages it into the SFU. The director subscribes via the livekit-cpp SDK and receives decoded frames.

### Root cause analysis

**1. Ingress jitter buffer (~1000 ms baseline)**

The LiveKit Ingress runs a WebRTC jitter buffer on the incoming WHIP stream before forwarding to the SFU. This buffer defaults to approximately 1000 ms and is not configurable through the LiveKit Ingress API or the WHIP protocol. The ingress is designed for live-streaming use cases (OBS, Twitch), not sub-second broadcast monitoring.

**2. ICE handshake PTS offset on real networks (~800–900 ms on top of baseline)**

The GStreamer WHIP pipeline is set `PLAYING` before ICE/DTLS negotiation completes. The pipeline clock runs during the entire ICE handshake. With `do-timestamp=TRUE` on `appsrc`, the first encoded frame receives a PTS equal to the ICE handshake duration (typically 800–1500 ms on a real network, ~50 ms on loopback). The receiving libwebrtc jitter buffer permanently locks in this PTS value as its target playout offset — which explains why the real-network figure (~1900 ms) exceeds the loopback figure (~1015 ms) by approximately the ICE RTT.

**3. livekit-ffi jitter buffer (no public API)**

The livekit-cpp SDK internally calls `set_jitter_buffer_minimum_delay()` in the Rust FFI layer. This method is exposed in `webrtc-sys/src/rtp_receiver.rs` but is not surfaced through the public C++ API (`libwebrtc/src/rtp_receiver.rs` → `livekit/rtp_receiver.h`). The director token includes `MinPlayoutDelay: 0` / `MaxPlayoutDelay: 0` in the `RoomConfiguration`, but these are forwarded via LiveKit signalling to the SFU and do not directly set the libwebrtc jitter buffer floor.

## Approaches Attempted

### 1. Playout delay via director token `RoomConfiguration` (deployed)

**What**: Set `MinPlayoutDelay: 0` and `MaxPlayoutDelay: 0` on the `auth.AccessToken`'s `RoomConfiguration` when generating the director's LiveKit JWT.

**Result**: No measurable improvement. LiveKit forwards these as RTCP playout-delay hints, but the livekit-ffi SDK does not honour them when setting the libwebrtc jitter buffer minimum. The jitter buffer still targets whatever offset was established at stream start.

**Code**: `backend/internal/signaling/grpc.go`, `joinAsDirector`.

---

### 2. GStreamer `appsrc` base-time reset after ICE (deployed, partial fix)

**What**: After `gst_element_get_state()` confirms the WHIP pipeline reached `PLAYING` (meaning ICE + DTLS completed), reset the pipeline and `appsrc` base time to the current clock value. This makes the first pushed buffer receive a PTS of ~33 ms (one frame period) instead of ICE_time milliseconds.

**Result**: Reduces the real-network penalty from ~900 ms (ICE duration) to ~0 ms. The baseline ~1000 ms from the ingress jitter buffer remains. Testing showed the overall `video_lag_ms` moved from ~1900 ms to approximately ~1000 ms on real networks — confirming the ICE PTS offset was a real contributor.

**Code**: `networking/src/whip_publisher.cpp`, `WHIPPublisher::start()`.

---

### 3. Native LiveKit SDK publish — bypass ingress entirely (implemented, then reverted)

**What**: Replace the GStreamer WHIP pipeline with a direct `livekit::VideoSource` + `LocalParticipant::publishVideoTrack()` publish path. The camera would connect to the LiveKit room using the SDK, push raw I420 frames from `CameraCapture` directly, and let the SDK handle encoding internally (VP8/H.264 via libwebrtc's internal encoder).

**Why reverted**:
- Bypasses NVENC. The entire NVENC → H.264 encode pipeline is replaced by libwebrtc's software encoder, which runs at significantly higher CPU cost and loses the latency advantage of dedicated encoding silicon.
- Measured `video_lag_ms` with native publish: **~5900 ms** (worse than baseline), likely due to the SDK's encoder introducing additional buffering.
- The latency measurement itself was inaccurate because the timing semantics of the native SDK path are fundamentally different from the WHIP/ingress path.

**Conclusion**: Native publish is architecturally incompatible with the NVENC requirement.

---

### 4. Patch livekit-ffi to expose `set_jitter_buffer_minimum_delay` (considered, not attempted)

**What**: Modify `libwebrtc/src/native/rtp_receiver.rs` and surface it through `libwebrtc/src/rtp_receiver.rs` and `livekit/rtp_receiver.h` so the director-side SDK can call `set_jitter_buffer_minimum_delay(true, 0.0)`.

**Why not pursued**: Requires modifying vendored library files in `/home/abrown/livekit-cpp/client-sdk-rust/`. This creates a permanent fork maintenance burden that would break on every upstream SDK update. Patching also has to be redistributed for every client deployment, which is operationally expensive.

---

### 5. Measurement methodology rework — timestamp-based matching (deployed)

**What**: Replace FIFO oldest-pop matching of DC packets to video frames with a clock-offset-aware timestamp match driven by libwebrtc's `VideoFrameEvent::timestamp_us` (when populated, GKE) or by a running estimate of `video_lag` (fallback for `network_mode: host` docker-compose, where `timestamp_us` is never populated).

**Why**: FIFO matching is mathematically broken when DC and video frame arrival rates differ at the receiver — and they do, because libwebrtc's decoder paces output independently of WHIP RTP arrival rate. The reported `video_lag` was capped at `MAX_CAPTURE_QUEUE_SIZE / DC_arrival_rate`, which produced 6000 ms when DC arrival dropped to ~10/s, and ~1500–2000 ms in normal conditions. Neither was the true frame transit latency.

**Result**: GKE reports `video_lag ≈ 900 ms`; docker prod reports `video_lag ≈ 1000 ms`. Both reflect actual frame transit, not the queue artefact. Per-frame `ts_match_diff` is typically <30 ms, indicating tight matching.

**Code**: `client/src/directortransport.cpp` (`onFrameArrived` matching), `client/src/videotrack.cpp` (plumb `timestamp_us`), `client/src/session/session_controller.cpp` (pace DC sends from encoder output), `client/src/session/camera_latency_sender.cpp` (`reliable=true` on the latency data channel).

---

### 6. NVENC encoder pipeline depth reduction (deployed)

**What**: Add `surfaces=1`, `delay=0`, `zerolatency=1` to the NVENC `av_dict` options in addition to `tune=ull`.

**Why**: Despite `tune=ull`, NVENC was buffering ~6 frames internally before producing first output (default `surfaces` ≈ 16). At 30 fps that's ~200 ms baked into `dc_one_way`. Verified by timing `avcodec_send_frame` and `avcodec_receive_packet` directly: both were sub-ms; the latency was inside NVENC's internal pipeline.

**Result**: `dc_one_way` dropped from ~174 ms to ~1–3 ms. Total e2e latency dropped from ~1180 ms to ~1007 ms on loopback.

**Code**: `video-core/src/encode/nvenc_encoder.cpp`.

---

### 7. livekit-ffi patch — `setJitterBufferMinimumDelay` exposed (built, tested, reverted)

**What**: Built a private fork of `livekit-cpp` at `/home/abrown/Projects/github.com/AaronBrownDev/client-sdk-cpp/` that exposes `Track::setJitterBufferMinimumDelay(seconds)` through every layer (`webrtc-sys` C++ → `libwebrtc` Rust → `livekit` Rust → `livekit-ffi` protocol → C++ public API). Wired `DirectorTransport::onTrackSubscribed` to call `setJitterBufferMinimumDelay(0.0)` on each subscribed video track. Patched libs at `client-sdk-cpp/build-release/lib/`.

**Why pursued**: Attempts 1 and 5 had established that the playout-delay hint travels through LiveKit signalling but `livekit-cpp` ignores it at the public API level. The patch made the C++ side actually call libwebrtc's `SetJitterBufferMinimumDelay` instead of just receiving the hint.

**Result**: The call lands correctly (verified by log line emitted before the first decoded frame) but `video_lag` only dropped from ~1005 ms → ~994 ms (min) — about **8 ms saved**. Mean was unchanged within noise.

**Why it didn't move the needle**: The director-side libwebrtc subscriber jitter buffer was *already* near zero on this stack. The remaining ~1 s is **upstream** of the director — distributed across the LiveKit Ingress's WHIP receive jitter buffer, the Ingress→SFU forwarding, and the SFU→subscriber path. None of those are reachable from the C++ patch. The same logic explains why every server-side / token-side hint (attempts 1 and 5 above) also produced no measurable improvement: they all target receivers that aren't where the time is spent.

**Conclusion**: The director-side fork is not worth the per-deploy maintenance cost. **Reverted** in `direct-link` (`client/src/directortransport.cpp` no longer calls the new method; `CMakeLists.txt` points back at the unpatched upstream `livekit-cpp` build). The patched fork stays on disk for reference but is not consumed by the build.

**Code (reverted)**: would have been `client/src/directortransport.cpp::onTrackSubscribed` and `client/CMakeLists.txt` `LIVEKIT_DIR` / `LIVEKIT_BUILD_SUBDIR` overrides.

## Current State

| Component | Loopback | GKE | Notes |
|---|---|---|---|
| `dc_one_way_ms` | ~1 ms | ~1 ms (post-NVENC fix) | Bound by encoder pipeline depth and DC transit |
| `video_lag_ms` | ~1005 ms | ~900 ms | **LiveKit pipeline floor** — see "Known limitations" below |
| `display_gap_ms` | 0 ms (headless) / ~3 ms (GUI) | ~3 ms (GUI) | GPU swap |
| **Total** | **~1007 ms** | **~900–950 ms** | |

The measurement methodology itself (timestamp-based matching, estimated-lag fallback, NVENC pipeline cap) is fully landed and trustworthy. Future regressions in any of these components will be visible; the residual ~1 s is the LiveKit architectural floor and is not improvable from this side.

## Known limitations (LiveKit floor)

Sub-second `video_lag` is **not reachable on the LiveKit + Ingress + WHIP architecture without changing the architecture itself.** This was empirically confirmed by attempts 1, 2, 5, and 7 above. The relevant facts:

1. **Multiple jitter buffers in series.** The WHIP path is camera → [Ingress libwebrtc receiver, jitter buffer #1] → [Ingress libwebrtc sender] → [SFU forwarding queue] → [Director libwebrtc receiver, jitter buffer #2] → decode. Each hop has its own libwebrtc/pion buffer; none is individually responsible for the ~1 s but together they sum to it. WebRTC media transports are designed to over-buffer to absorb jitter — that's the design.

2. **None of the configurable knobs help.** `room.playout_delay` (server config), `MinPlayoutDelay/MaxPlayoutDelay` on either the director or ingress-participant token, the `playout-delay` RTP extension hint, and even directly calling `SetJitterBufferMinimumDelay(0)` on the director's libwebrtc receiver (the patched-fork experiment in attempt 7) all touch *one* of those buffers — and even then libwebrtc treats the value as a *minimum target*, not a hard cap. They produced 0–10 ms of measurable change combined.

3. **LiveKit's design target is ~1–2 s latency** (live event streaming, conference room media, OBS-style ingest). It is *not* a sub-second broadcast monitoring stack. This isn't a bug in LiveKit; it's its operating point.

For DirectLink's stated <150 ms goal, **the path forward is architectural, not configurational** — see "Future options" below for the realistic shapes. Until one of those is undertaken, the measurement reports the true floor of the current architecture.

## Future options

These are listed for completeness; **none are MVP-scope** given the size of the LiveKit investment in this codebase. Pick from this list only if a future product decision makes <150 ms a hard requirement.

1. **Direct WebRTC peer-to-peer between camera and director.** Use the existing gRPC server for SDP exchange, no SFU, no Ingress. One libwebrtc connection, one jitter buffer. Realistically 100–200 ms total. Cost: lose multicast — if multiple directors per session is required, need a separate fanout strategy. Several hundred lines of code.

2. **Custom thin WHIP→RTP forwarder** (replacing the LiveKit Ingress in the camera path while keeping LiveKit for everything else). Few hundred lines of GStreamer or pion. Removes one of the in-series jitter buffers; total gain probably 500-700 ms.

3. **Patch `livekit/ingress` (Go).** Same vendoring pattern as the `livekit-cpp` experiment in attempt 7, but on the ingress server. Theoretically attacks the upstream-side jitter buffer that attempt 7 couldn't reach. Per the data so far, the actual gain is uncertain — every LiveKit-internal knob attempted has produced negligible savings, suggesting the architectural floor may be deeper than any one buffer.

4. **`timestamp_us` not populated on docker-compose `network_mode: host`.** Cosmetic — docker prod uses the estimated-lag fallback (which works fine, just slightly less precise than the GKE timestamp-based path). Worth investigating only if dev-loop precision matters.

The measurement infrastructure landed in this branch will validate any of the above as real wins (or not) without ambiguity.
