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

## Current State

| Component | Loopback | Notes |
|---|---|---|
| `dc_one_way_ms` | ~1 ms | Bound by encoder pipeline depth (now ~1–3 ms) and DC transit |
| `video_lag_ms` | ~1005 ms | LiveKit Ingress jitter buffer floor — not configurable in current versions |
| `display_gap_ms` | 0 ms (headless) / ~3 ms (GUI) | GPU swap |
| **Total** | **~1007 ms** | Within MVP-acceptable for director-monitor use case |

GKE-reported numbers from a real deployment (`video_lag ≈ 900 ms`, total ~1250 ms with ~285 ms `dc_one_way` before the NVENC fix; expected to be similar to loopback after NVENC fix is deployed).

## Open Questions / Future Options

1. **livekit-ffi patch (option #4 above) remains the only known path to sub-second `video_lag`.** Defer until MVP requirements demand it; would need a story for distributing the patched build to all client deployments.

2. **Add `playout-delay` RTP extension to the GStreamer SDP.** The extension is in the WebRTC spec and libwebrtc honours it on the receiver side, but the current `whipsink` (`gst-plugins-rs` 0.14.4) does not expose RTP extension negotiation through public properties. Would require forking `whipsink` or replacing it with raw `webrtcbin` + a hand-rolled WHIP HTTP exchange.

3. **Investigate why `timestamp_us` is `0` on docker-compose with `network_mode: host`.** Probably an interaction between loopback and libwebrtc's RTCP SR clock-sync path. If fixed, the docker prod measurement uses the same timestamp-based path as GKE (more accurate) instead of the estimated-lag fallback.

4. **LiveKit Ingress `JitterBufferMs` config.** Not present in protocol v1.45; track upstream releases for an exposed knob.
