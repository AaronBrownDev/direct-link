# CameraLatencySender WebRTC Debug Log

## Goal

Get `CameraLatencySender` to successfully connect to a LiveKit room as a data-only
participant and publish latency timestamp packets via a WebRTC data channel.
Verified end-to-end via the `test_latency_sender` integration test.

---

## Root Cause Chain

### Fix 1 — Missing `CanPublishData` in JWT (GKE, deployed)

**Symptom:** `wait_pc_connection timed out` immediately on connect.

**Cause:** The camera data token was issued without `CanPublishData: true`. LiveKit
server skips WebRTC negotiation entirely for participants that have no publish
permissions at all.

**Fix** (`backend/internal/signaling/grpc.go`, `joinAsCamera`):
```go
canPublish, canSubscribe, canPublishData := false, false, true
dataGrant := &auth.VideoGrant{
    RoomJoin:       true,
    Room:           sess.ID,
    CanPublish:     &canPublish,
    CanSubscribe:   &canSubscribe,
    CanPublishData: &canPublishData,
}
```

**Status:** Deployed to GKE dev cluster.

---

### Fix 2 — Wrong `node_ip` in LiveKit k8s config (GKE, deployed)

**Symptom:** `wait_pc_connection timed out` persisted after Fix 1.

**Cause:** `node_ip` was set to the LoadBalancer IP (`34.174.171.158`), which only
forwards TCP 7880/7881. WebRTC UDP 50000-60000 had no forwarding path through the LB.

**Fix** (`infrastructure/kubernetes/overlays/dev/patches/livekit-config.yaml`):
Removed hardcoded `node_ip`. Kept `use_external_ip: true` so LiveKit queries the GCP
metadata service / STUN at pod startup to auto-detect the node's actual public IP.

**Status:** Deployed to GKE dev cluster. LiveKit logs confirm `nodeIP: 34.174.17.20`
(node's direct ephemeral IP). UDP ICE candidates now reach LiveKit directly.

---

### Fix 3 — Docker NAT blocking local WebRTC (local, deployed)

**Symptom:** `test_signaling_client` passed but `test_latency_sender` timed out against
local docker-compose stack.

**Cause:** Default Docker bridge networking NATs WebRTC UDP traffic. LiveKit can't
receive ICE candidates from the client.

**Fix** (`docker-compose.prod.yaml`): Switched all services to `network_mode: host`.
LiveKit uses `--node-ip 127.0.0.1`. All inter-service URLs changed to `localhost`.

**Status:** Local stack works for signaling. WebRTC layer still failing (see below).

---

## Fix 4 — livekit-ffi Rust SDK skips initial publisher offer (local, **RESOLVED**)

**Root cause:** A bug in livekit-ffi v0.12.x (`rtc_session.rs:533`). The Rust SDK was
ported from the JS SDK but the initial negotiation condition was transcribed
incorrectly:

```
JS SDK:   if (!this.subscriberPrimary || joinResponse.fastPublish) { this.negotiate(); }
Rust SDK: if single_pc_mode || join_response.fast_publish { ... }  // missing !subscriberPrimary
```

When the server responds with `subscriber_primary: false` (publisher is primary) and
neither `single_pc_mode` nor `fast_publish` is true, the Rust SDK never sends the
initial SDP offer from the publisher PC. Data channels are never established, and the
server drops the participant after `ping_timeout` (15 s).

**Fix** (`client/src/session/camera_latency_sender.cpp`):

Set `opts.single_peer_connection = true`. This requests the v1 signal path from the
server, which returns `single_pc_mode=true`, which triggers `publisher_negotiation_needed()`
immediately. The single PC sends its SDP offer, data channels are negotiated, and the
connection completes.

```cpp
opts.auto_subscribe = false;
opts.single_peer_connection = true;   // workaround for livekit-ffi v0.12.x bug
```

**Verified:** `test_latency_sender` exits 0 locally. Debug log confirms:
- `path=v1, single_pc_mode=true`
- `sending publisher offer` → `received publisher answer` (data channel SDP)
- 5-second latency stream completed successfully

---

## RESOLVED — all local tests passing

---

## Previous Blocker (for reference) — `wait_pc_connection timed out` (local, unresolved)

### Symptom

`test_latency_sender` against local `docker-compose.prod.yaml` stack:
- gRPC signaling: fully working (session created, director joined, camera joined, correct tokens returned)
- `CameraLatencySender.start()`: 15-second timeout, never emits `connected()`

### Debug log evidence (`RUST_LOG=livekit=debug`)

```
[livekit_api::signal_client] signal connection successful: path=v0, single_pc_mode=false
JoinResponse: can_publish_data=true, subscriber_primary=false, ping_timeout=15
PeerConnection created (libwebrtc log)
<< NO SDP offer/answer exchange seen >>
[after ~14s] server closed the connection — reason: RoomDeleted
[livekit::rtc_engine] failed to connect: Connection("wait_pc_connection timed out"), retrying... (1/3)
```

LiveKit server side: `"removing participant without connection"`, `"connectionType": "unknown"`.

### Root Cause Hypothesis

`subscriber_primary: false` in the JoinResponse means the **publisher PC** is the
primary PeerConnection (old livekit behavior). In this mode the SDK is expected to
send the SDP offer from the publisher PC. However, the livekit-ffi Rust SDK
(v0.12.48) appears not to initiate publisher PC negotiation when the participant has
no media tracks to publish — even with `CanPublishData: true`. The data channel
therefore never gets established and the server drops the participant after
`ping_timeout` (15 s).

The director transport works because `CanSubscribe: true` forces subscriber PC setup,
which happens to succeed.

### Options to Investigate

1. **Add `CanSubscribe: true` to the data token** — forces subscriber PC negotiation.
   The `auto_subscribe: false` RoomOptions prevents actual track subscriptions.
   Tradeoff: grants unnecessary subscribe permission in the JWT.

2. **Add `rtc.subscriber_primary: true` to livekit.yaml** — makes subscriber PC
   primary server-wide. Data channels route through subscriber PC and negotiation is
   always triggered. Cleaner; no token permission change needed.

3. **Check livekit-ffi v0.12.48 for data-only participant bug** — may be a known
   issue with a patch or newer SDK version.

4. **Inspect livekit-cpp RoomOptions** for a flag that forces PC negotiation
   (e.g., `dynacast`, `adaptiveStream`, or a custom data-channel option).

### Resolution

See Fix 4 above. `opts.single_peer_connection = true` was the correct fix.

---

## Test Matrix

| Test | Stack | Status |
|---|---|---|
| `test_signaling_client` | local docker-compose | PASS |
| `test_livekit_room` (director) | local docker-compose | PASS |
| `test_latency_sender` | local docker-compose | **PASS** |
| `test_latency_sender` | GKE dev | Pending — needs re-run after Fix 4 |
| Operator client end-to-end | GKE dev | Pending |

---

## Files Modified

| File | Change |
|---|---|
| `backend/internal/signaling/grpc.go` | Added `CanPublishData: true` to camera data JWT |
| `infrastructure/kubernetes/overlays/dev/patches/livekit-config.yaml` | Removed hardcoded `node_ip`; rely on `use_external_ip: true` |
| `backend/configs/livekit.yaml` | `redis.address` → `localhost:6379`; webhook → `localhost:8081` |
| `docker-compose.prod.yaml` | All services → `network_mode: host`; LiveKit `--node-ip 127.0.0.1` |
| `client/src/session/camera_latency_sender.hpp` | Added `connected()` and `connectionFailed()` signals |
| `client/src/session/camera_latency_sender.cpp` | Emit new signals on connect success/failure |
| `client/tests/test_latency_sender.cpp` | New integration test for camera data path |
| `client/CMakeLists.txt` | Added `test_latency_sender` build target |
| `client/tests/test_signaling_client.cpp` | Fixed `cameraJoined` to 4-param signature; added `--server` flag |
| `client/tests/test_operator_session.cpp` | Fixed `cameraJoined` to 4-param signature |
| `client/tests/test_livekit_room.cpp` | Added `--server` flag |
