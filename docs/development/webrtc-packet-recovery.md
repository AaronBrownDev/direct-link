# WebRTC Packet Recovery (NACK + RTX)

## Why this exists

The publish path is `appsrc → h264parse → rtph264pay → whipsink → webrtcbin →
nicesink → UDP → LiveKit ingress`.  Once a packet leaves `nicesink` it
travels over the public internet via UDP, and UDP doesn't retransmit on its
own.  When a single fragment of an H.264 IDR is dropped on the way to the
ingress, the receiver's decoder freezes on the last good frame until the
next keyframe arrives — usually 0.5 s to several seconds later, depending on
GOP size and whether a PLI round-trip succeeds.

For a latency-measurement MVP this is catastrophic: a single 0.01 % loss
event hides every frame for ~500 ms and pollutes the entire run.  The
mechanisms in this document give the receiver a way to repair small losses
without waiting for an IDR.

## Mechanisms in scope

| Mechanism | Status | Latency impact |
|---|---|---|
| **NACK** (RFC 4585) — receiver tells sender "I lost seq #12345" | **Enabled** | None on clean path; ~1 RTT on loss |
| **RTX** (RFC 4588) — sender retransmits lost packets on a parallel SSRC/PT | **Enabled** | None on clean path; ~1 RTT on loss |
| **PLI** (Picture Loss Indication) — receiver asks for a fresh IDR | **Enabled** (was already on) | High — waits for an IDR-sized burst |
| **FEC** (ULPFEC/RED, RFC 5109) — sender pre-emptively duplicates data | Not enabled | Adds 10–30 % bandwidth, ~0–5 ms |
| **Simulcast / SVC** | Not enabled | Variable; large change to publish pipeline |

We deliberately implement only the lossless-when-clean mechanisms (NACK +
RTX).  FEC adds bandwidth even when no loss occurs, which on tight upload
links can itself induce loss.  Simulcast is out of scope for an MVP that
publishes a single quality.

## How NACK + RTX cooperate

```
operator                                                        ingress
  │                                                                │
  │  RTP seq=42 (PT=96, H.264 fragment)            ─────►          │
  │  RTP seq=43 (PT=96, H.264 fragment)            ─ X (lost)      │
  │  RTP seq=44 (PT=96, H.264 fragment)            ─────►          │
  │                                                                │
  │  ◄──────  RTCP Generic NACK (PID=43)                           │
  │                                                                │
  │  rtprtxsend looks up seq 43 in its packet history,             │
  │  re-emits the payload on the RTX SSRC with PT=97               │
  │                                                                │
  │  RTX  seq=N (PT=97, original=43)               ─────►          │
  │                                                                │
  │                          receiver re-orders into seq=43 slot   │
  │                          and hands the now-complete frame to   │
  │                          the decoder                           │
```

The cost on a clean path is the small per-packet history that the sender
maintains in memory.  The cost on a lossy path is one round trip of extra
end-to-end delay for the recovered frame, which is far cheaper than waiting
for the next keyframe.

## What the publisher offers in the SDP

`networking/src/whip_publisher.cpp` builds the caps it hands to
whipsink's `sink_%u` request pad.  The relevant lines are:

```cpp
GstCaps *rtp_caps = gst_caps_new_empty();

// PT 96 — actual H.264 video.
gst_caps_append_structure(rtp_caps,
    gst_structure_new("application/x-rtp",
        "media",         G_TYPE_STRING, "video",
        "encoding-name", G_TYPE_STRING, "H264",
        "payload",       G_TYPE_INT,    96,
        "clock-rate",    G_TYPE_INT,    90000,
        nullptr));

// PT 97 — RTX, retransmissions for PT 96 (apt = "associated payload type").
gst_caps_append_structure(rtp_caps,
    gst_structure_new("application/x-rtp",
        "media",         G_TYPE_STRING, "video",
        "encoding-name", G_TYPE_STRING, "rtx",
        "payload",       G_TYPE_INT,    97,
        "clock-rate",    G_TYPE_INT,    90000,
        "apt",           G_TYPE_INT,    96,
        nullptr));
```

webrtcbin sees both structures, advertises them in the offer, and
auto-instantiates an internal `rtprtxsend` element wired up to the same
session.  The resulting offer SDP looks like:

```
m=video 9 UDP/TLS/RTP/SAVPF 96 97
a=rtpmap:96 H264/90000
a=rtcp-fb:96 nack pli
a=rtcp-fb:96 ccm fir
a=rtcp-fb:96 transport-cc
a=fmtp:96 packetization-mode=1; ...
a=rtpmap:97 rtx/90000
a=fmtp:97 apt=96
```

The `apt=96` line is what the receiver uses to map RTX-stream packets back
into the H.264 stream's sequence space.

## What the publisher sets on the transceiver

After whipsink creates the inner webrtcbin and its sendonly transceiver, we
reach in and explicitly enable NACK handling:

```cpp
GstElement *webrtcbin =
    gst_bin_get_by_name(GST_BIN(whipsink), "whip-webrtcbin");
GArray *transceivers = nullptr;
g_signal_emit_by_name(webrtcbin, "get-transceivers", &transceivers);
for (guint i = 0; i < transceivers->len; ++i) {
    GObject *t = g_array_index(transceivers, GObject *, i);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(t), "do-nack")
            != nullptr) {
        g_object_set(t, "do-nack", TRUE, nullptr);
    }
}
```

`do-nack=TRUE` instructs the webrtcbin internals to honour incoming RTCP
NACK reports by retransmitting the requested sequence numbers via RTX.
Without this, NACK reports are received and parsed but ignored, and we'd
still be relying on PLI-only recovery despite the SDP advertising RTX.

The property-existence guard (`g_object_class_find_property`) keeps the
code working if it's ever compiled against a webrtcbin that doesn't expose
the property — we fall back to whatever default the build ships.

## Latency tradeoffs

What this **does not** add to the success path:
- No extra encoder-side delay.  rtprtxsend buffers packets for retransmit,
  but it does not delay the original send.
- No extra payloader-side delay.  rtph264pay still emits packets at line
  rate.

What this **does** add when the network is lossy:
- A small jitter buffer on the receiver.  WebRTC's video receive path holds
  decoded frames briefly so an in-flight retransmit has a chance to land
  before the frame is rendered.  Typical contribution is 10–30 ms on
  uncongested links, scaling with measured RTT.  This is the price of
  recovery; without it a lost packet is a freeze.

What it removes:
- 500 ms – several seconds of "frozen on last good frame" during loss
  events.  Replaced with 1× round-trip of holdback for the dropped fragment.

For our MVP — measuring camera-to-display latency on real networks — the
constant ~10–30 ms cost is a strict win over occasional half-second
freezes.

## Tuning knobs (not currently set)

If we ever need finer control, webrtcbin exposes:

- `latency` (default 200 ms) — receiver-side jitter buffer cap.  Lowering
  this trades robustness for latency on the receive path; raising it gives
  more time for retransmits to land.  Set on the inner webrtcbin via
  `g_object_set(webrtcbin, "latency", new_ms, nullptr)`.
- `rtx-time` on the internal rtprtxsend — sender-side history depth.
  Default is generally enough (200 ms).  Lower to save memory; raise if
  RTT exceeds the default.

These aren't exposed by whipsink directly; like `do-nack`, they require
reaching into the inner webrtcbin.  We haven't found a need to override
either yet.

## What to verify after a code change

If the publisher pipeline construction changes and you want to confirm
NACK + RTX is still wired up:

1. Run the operator with `GST_DEBUG="whipsink:5,webrtcbin:4"`.
2. In the offer SDP printed by whipsink, confirm:
   - `m=video 9 UDP/TLS/RTP/SAVPF 96 97` (both PTs in the m-line)
   - `a=rtpmap:97 rtx/90000`
   - `a=fmtp:97 apt=96`
3. Either:
   - Trigger packet loss (e.g. `tc qdisc add dev eth0 root netem loss 1%`)
     and observe that the receiver's video does not freeze the way it would
     without recovery, *or*
   - Bump `webrtcbin:6` and look for "Sending RTX" or rtprtxsend log lines
     when the receiver issues NACK.

## Related code

- `networking/src/whip_publisher.cpp` — caps + transceiver configuration
- `video-core/src/encode/software_encoder.cpp` — libx264 settings tuned
  to match Chrome's libwebrtc wrapper:
  - `slice-max-size=1200` so each frame is split into MTU-sized slices.
    Each slice fits in one RTP packet; a lost packet damages only that
    slice instead of the whole frame.
  - `ref=1` to match the single-reference-frame model used by NVENC and
    VAAPI in zerolatency mode.  Limits how far decode errors propagate.
  - `intra-refresh=1`, `no-scenecut=1` for gradual recovery instead of
    monolithic IDRs at fixed GOP boundaries.
- `video-core/src/encode/nvenc_encoder.cpp` — uses NVENC's `delay=0`,
  `zerolatency=1`, single-surface mode.  Output already has WebRTC-friendly
  packetization characteristics by default.
- `video-core/src/encode/vaapi_encoder.cpp` — AMD VCN / Intel QSV via
  libva.  Uses `rc_mode=CBR` and `low_power=1` (where supported) to get
  flat, low-latency packet pacing.

## Encoder choice and packet-loss tolerance

The three encoders behave very differently on lossy paths even before
NACK+RTX gets involved.  Empirically:

| Encoder    | Default WebRTC compatibility | Notes                                  |
|------------|------------------------------|----------------------------------------|
| `h264_nvenc` | Excellent                  | NVIDIA's WebRTC-aware defaults         |
| `h264_vaapi` | Excellent                  | CBR + LP profile produces flat output  |
| `libx264`    | Poor *out of the box*; good with the overrides above | Default settings are for offline transcoding, not real-time |

Without the slice-max-size and intra-refresh overrides, libx264 can
publish video that flows on a clean LAN but freezes catastrophically on
any internet path with even mild packet loss.  Treat the override list as
the floor for the software fallback, not as optional tuning.

## Future work

The remaining recovery / robustness mechanisms we have not implemented,
ordered by likely benefit-to-cost ratio for our use case (a latency MVP
publishing a single H.264 stream to a LiveKit ingress).

### FEC — Forward Error Correction (ULPFEC + RED)

The receiver reconstructs a missing packet from redundant data sent
proactively, *without* a round-trip to the sender.

**What we'd change:** Add `ulpfec` and `red` payload types to the caps
offered to whipsink's request pad — same pattern as RTX.  Set the
appropriate transceiver / session properties so webrtcbin's internal FEC
encoder is wired up.

**Costs:**
- **Bandwidth: +10-30 % constant overhead.**  FEC sends parity packets
  whether loss occurs or not.  On tight upload pipes this can itself
  induce loss, which is counter-productive.
- **Sender CPU: small, FEC packets are cheap.**
- **Receiver latency: +0-5 ms** (parallel decode of parity, not
  serialized after the data packets).

**Benefits:**
- Single-packet losses on the wire are reconstructed without an RTT
  delay — much faster than NACK/RTX for occasional small losses.
- Best on paths where RTT is high enough that NACK round-trips arrive
  too late for the receiver to re-render the frame in time.

**When to add:** If real-world testing on a representative network shows
NACK alone has visible recovery delays.  Skip on guaranteed-low-RTT
paths (LANs, regional WebRTC) where NACK+RTX is enough.

### Simulcast — multiple encoded layers

Publish 2–3 quality layers (e.g. 1080p / 720p / 360p) simultaneously.
The receiver / SFU picks the appropriate layer based on its conditions.

**What we'd change:**
- Run multiple encoder instances in parallel (or use SVC if supported).
- Multiple `rtph264pay` branches feeding webrtcbin via separate
  transceivers.
- Negotiate `a=rid` / `a=simulcast` SDP extensions.
- Keep N versions of the captured frame (or share via filter graph).

**Costs:**
- **Substantial pipeline restructure** in `whip_publisher.cpp` and
  `video_pipeline.cpp`.
- **Sender CPU: roughly N× encoding** (or somewhat less with SVC).
- **Sender bandwidth: ~1.3-1.7× single-stream** (lower layers add a
  small fraction).
- **Receiver complexity: requires SFU support** to pick layers.
  LiveKit supports simulcast natively, so no new server work.

**Benefits:**
- Receiver can fall back to a lower layer during congestion instead of
  freezing on the high-quality layer.
- Useful when serving multiple receivers with different network
  conditions (one on LAN, one on cellular).

**When to add:** When the app evolves beyond "one operator publishes to
one director" — simulcast is mostly waste for a 1:1 path with a single
fixed-bitrate target.

### Bandwidth-adaptive bitrate

Adjust the encoder's target bitrate at runtime based on `transport-cc`
feedback from the receiver.  Today our encoder runs at a fixed bitrate
computed from pixel rate; transport-cc feedback is parsed by webrtcbin
but never reaches the encoder.

**What we'd change:**
- Subscribe to webrtcbin's bitrate-estimation signal (or scrape stats
  via `g_signal_emit "get-stats"`).
- Hook a callback that calls `av_opt_set_int(codecCtx_, "bit_rate",
  new_value, 0)` or equivalent — each encoder needs the right call:
  - NVENC: `av_opt_set` on `b` and `maxrate` properties live (NVENC
    supports dynamic reconfiguration without a flush).
  - VAAPI: same, via `av_opt_set_int` on `bit_rate`.
  - libx264: less reliable runtime reconfiguration; may need a `flush
    + open` cycle.
- Throttle the rate of changes (e.g. 1× per second) so the encoder
  doesn't oscillate.

**Costs:**
- **Code complexity: moderate.**  ~100-200 lines of plumbing across
  pipeline, encoder base class, each encoder's `setBitrate`.
- **Bitrate behaviour becomes non-deterministic** — slightly harder to
  reason about for latency measurements.

**Benefits:**
- During congestion, the encoder backs off instead of dropping packets
  at the bottleneck.  This is the single biggest "real internet"
  reliability improvement we don't yet have.
- Implicitly helps recovery: lower bitrate means smaller frames means
  fewer per-frame fragments means fewer per-frame loss events.

**When to add:** Soon, if the use case ever involves networks more
unpredictable than our test setup.  This and FEC are the two highest-
impact items for "real internet" deployment.

### Receiver-side jitter buffer tuning

`webrtcbin`'s `latency` property (default 200 ms) caps the receiver
buffer.  Lower is better for latency measurement; higher is better for
loss recovery.  We don't currently override it.

**Cost:** trivial — one property set on the inner webrtcbin.
**Benefit:** small but immediate latency win for clean LANs (drop to
50-100 ms) at the expense of loss tolerance.
