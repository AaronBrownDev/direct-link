# Encoder Strategy

This document explains how `direct-link` chooses an H.264 encoder at
runtime, why three different paths exist, what the differences mean for the
WebRTC publish path, and the rationale behind the per-encoder
configuration.  It complements
[`webrtc-packet-recovery.md`](./webrtc-packet-recovery.md), which focuses
on NACK / RTX / loss handling at the transport layer.

## TL;DR

We probe encoders in this order and pick the first one that opens at
runtime:

1. **`h264_nvenc`** — NVIDIA hardware via NVENC
2. **`h264_vaapi`** — AMD VCN or Intel Quick Sync via libva
3. **`libx264`** — software fallback

The probe at process start checks whether each encoder is *compiled in* to
FFmpeg.  If the chosen encoder fails its actual `avcodec_open2` call (e.g.
NVENC compiled in but `libcuda.so.1` missing on a non-NVIDIA host), the
`VideoPipeline::start` retry chain walks down the list in the same order.

The result: every supported Linux host gets *some* working encoder, with
the best available hardware path used automatically and software always
present as a final safety net.

## Why three encoders, not one

Software-only would be portable but has the worst latency and the most
fragile WebRTC behaviour.  Hardware-only would force every user to have a
specific GPU vendor.  Each encoder occupies a different point on the
portability ↔ performance ↔ packet-loss-tolerance triangle.

| Encoder | Vendor | Frame encode time @ 1080p60 | Power | WebRTC defaults | Portability |
|---|---|---|---|---|---|
| `h264_nvenc` | NVIDIA | ~1 ms | GPU (NVENC ASIC) | Excellent — vendor tunes for WebRTC use cases | NVIDIA + driver |
| `h264_vaapi` | AMD / Intel | ~1-3 ms | GPU (VCN / QSV ASIC) | Excellent — CBR + LP profile produces WebRTC-friendly output | Mesa or intel-media-driver, `/dev/dri/renderD128` |
| `libx264` | Anyone | ~5-15 ms | CPU | Poor out of the box; usable after override list | Any Linux + libavcodec-extra |

The "frame encode time" column is the *encode* portion only — actual
end-to-end latency is dominated by capture, transport, and receiver-side
jitter buffer.  But encoders that take longer than the frame budget
(16.6 ms at 60 fps) introduce bursty publishing that downstream WebRTC
buffers don't handle gracefully.

## Class layout

```
videoCore::encode::Encoder           (abstract; defines initialize/encode/stop)
├── NVENCEncoder                     (FFmpeg h264_nvenc wrapper)
├── VAAPIEncoder                     (FFmpeg h264_vaapi wrapper + hwframes pool)
└── SoftwareEncoder                  (FFmpeg libx264 wrapper)
```

`createEncoder(config, allowHardware)` in
`video-core/src/encode/encoder.cpp` is the factory.  It returns the chosen
implementation but does **not** call `initialize()` — that is the caller's
responsibility (today: `VideoPipeline::start`).

The split is deliberate: probing whether `h264_nvenc` is *compiled in* is
cheap (`avcodec_find_encoder_by_name`), but probing whether it actually
*works* requires opening a real codec context with a hardware device, which
is expensive and side-effecting.  The factory does only the cheap probe;
the runtime open happens once during pipeline start.

## Selection logic

`createEncoder` (in `encoder.cpp`):

```cpp
if (resolved.type == Software && allowHardware) {
    if (h264_nvenc available)       resolved.type = NVENC;
    else if (h264_vaapi available)  resolved.type = VAAPI;
}
else if (resolved.type == NVENC) {
    if (!allowHardware || !h264_nvenc available) {
        if (allowHardware && h264_vaapi available)  resolved.type = VAAPI;
        else                                         resolved.type = Software;
    }
}
else if (resolved.type == VAAPI) {
    if (!allowHardware || !h264_vaapi available)    resolved.type = Software;
}
```

Two callers exist:

- **`CameraSession::start`** sets `config.type = Software` (default) and
  passes `allowHardware = true`, so the factory does the full probe and
  picks the best available path.
- **`VideoPipeline::start`** (on retry after an init failure) constructs a
  `cfg` with an explicit type from the fallback chain and passes
  `allowHardware = true` for hardware types or `false` for `Software` so
  the factory doesn't auto-upgrade right back to whatever just failed.

## Runtime fallback in `VideoPipeline::start`

```cpp
auto result = encoder_->initialize(encoderConfig_, onEncodedPacket);
if (result != Success) {
    const Type chain[] = { VAAPI, Software };
    for (auto type : chain) {
        cfg.type = type;
        encoder_ = createEncoder(cfg,
                                 /*allowHardware=*/(type != Software));
        if (!encoder_) continue;
        result = encoder_->initialize(cfg, onEncodedPacket);
        if (result == Success) break;
        encoder_.reset();
    }
}
```

The chain is hard-coded to NVENC → VAAPI → Software because:

- NVENC (the most common first pick) failing at runtime is almost always
  "no NVIDIA libs" — meaning the host has a different GPU; VAAPI is the
  next most likely to succeed.
- VAAPI failing usually means no DRI render node, no driver installed, or
  permissions are wrong — none of those have a hardware alternative on the
  same host, so we fall to software.
- Software (libx264) has no runtime dependency that can fail.

Each step prints `[Encoder] selected: ...` so a tail of stderr always
reveals which path is in use.

## Why each encoder needs different code, not just different options

You might expect one `Encoder` class that switches behaviour by codec
name.  We can't, because the three paths have fundamentally different
input requirements:

### NVENC — `NVENCEncoder`

Takes plain `AV_PIX_FMT_YUV420P` frames in CPU memory.  FFmpeg handles the
upload to GPU memory transparently because NVENC's AVCodec implementation
manages its own CUDA context.  There is no `hw_device_ctx` setup at the
application level — we just `avcodec_open2` and feed frames.

### VAAPI — `VAAPIEncoder`

Requires hardware-resident `AV_PIX_FMT_VAAPI` frames.  Plain YUV420P
frames are rejected.  Three setup steps:

1. `av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_VAAPI)` opens libva and binds
   to a DRI render node.
2. `av_hwframe_ctx_alloc` + `av_hwframe_ctx_init` allocates a pool of GPU
   surfaces (NV12-formatted, sized to the encode resolution).
3. `codec_ctx->hw_frames_ctx = av_buffer_ref(...)` ties the encoder to the
   pool so it can hand out surfaces during encode.

Per frame: convert the incoming I420 to NV12 in CPU memory (libswscale),
then upload to a hardware surface via `av_hwframe_transfer_data` before
sending to the encoder.  The cost is a single GPU upload per frame —
negligible at 1080p60 on modern AMD/Intel.

### libx264 — `SoftwareEncoder`

Takes plain YUV420P, no hardware setup.  Simplest of the three to bring
up; hardest to make produce a WebRTC-friendly bitstream out of the box.

## Bitstream characteristics that matter for WebRTC

The encoders differ not just in speed but in the *shape* of the bitstream
they emit.  These properties dominate whether the publish path works
under realistic packet loss:

### Slice / NAL size

WebRTC payload (rtph264pay → webrtcbin) splits NAL units across RTP
packets when they exceed MTU (~1450 bytes after SRTP overhead).  A 1080p
IDR is typically 100-200 KB, which fragments into 70+ packets.  Lose any
one and the entire frame is undecodable until the next keyframe.

- **NVENC and VAAPI** produce small, regular frames by default — multiple
  slices per frame internally — so even when the wire-side packet count is
  high, individual losses are recoverable per-slice.
- **libx264** by default emits one big NAL per frame.  We override this
  with `slice-max-size=1200`, which forces each slice to fit in one MTU.
  Without this override, libx264's WebRTC behaviour on lossy paths is
  catastrophic.

### Reference frames

Number of past frames the decoder needs to keep around to decode the
current frame.  More references = better compression, but errors propagate
across more frames if anything in the reference window is corrupted.

- **NVENC and VAAPI** in zerolatency / low-power profiles use `ref=1`.
- **libx264** defaults to `ref=3`.  We override to `ref=1` to match.

### Rate control

How the encoder allocates bits across frames.

- **NVENC** with `tune=ull` (ultra-low-latency) does CBR-like output
  with each frame approximately the same size, which the receiver's
  jitter buffer copes with cleanly.
- **VAAPI** with `rc_mode=CBR` does true CBR.  Same property.
- **libx264** defaults to CRF (constant rate factor), which produces wildly
  uneven frame sizes.  We don't fully fix this — running CBR in libx264
  is possible but loses much of its quality advantage.  Combined with
  `slice-max-size`, the per-packet impact is bounded even if the
  per-frame size still varies.

### IDR handling and intra-refresh

Recovery from severe loss requires either an IDR (full keyframe) or a
non-trivial alternative like periodic intra refresh.

- **NVENC** emits IDRs at fixed GOP boundaries (default ~1 s) and on PLI.
  IDRs are big but well-paced.
- **VAAPI** same model, configurable via `gop_size`.
- **libx264** with `intra-refresh=1` *replaces* IDRs at GOP boundaries
  with a moving column of intra macroblocks — each frame is roughly the
  same size, no big IDR bursts.  The catch: forced-IDR (PLI handling)
  still emits a full IDR, so PLI flooding partially defeats the
  refresh design.

### Force keyframe (PLI) handling

The receiver requests a fresh keyframe via PLI when its decoder has
errored.  All three encoders accept `pict_type = AV_PICTURE_TYPE_I` to
force an IDR on the next frame.  WHIPPublisher's appsrc-src probe
intercepts the upstream `GstForceKeyUnitEvent` and routes it to the
encoder's `requestKeyframe()` method.  This wiring is shared across all
three.

## Per-encoder config (current state)

Most relevant lines from each.

### NVENC (`nvenc_encoder.cpp`)

```cpp
av_dict_set(&options, "preset", "p1", 0);          // fastest
av_dict_set(&options, "tune", "ull", 0);           // ultra-low-latency
av_dict_set(&options, "surfaces", "1", 0);         // no internal queue
av_dict_set(&options, "delay", "0", 0);
av_dict_set(&options, "zerolatency", "1", 0);
codecCtx_->max_b_frames = 0;
```

Tested NVIDIA-recommended settings for live publishing — minimal latency,
no buffer.

### VAAPI (`vaapi_encoder.cpp`)

```cpp
av_dict_set(&options, "rc_mode", "CBR", 0);
av_dict_set(&options, "quality", "0", 0);          // best speed
av_dict_set(&options, "low_power", "1", 0);        // VCN/QSV LP path
codecCtx_->max_b_frames = 0;
```

`low_power=1` is retried without it on init failure for older AMD VCN
drivers that reject the flag.  CBR rate control gives the flat output
shape that WebRTC needs.

### libx264 (`software_encoder.cpp`)

```cpp
av_dict_set(&options, "tune", "zerolatency", 0);
av_dict_set(&options, "profile", "baseline", 0);
av_dict_set(&options, "x264-params",
            "slice-max-size=1200:ref=1:intra-refresh=1:no-scenecut=1", 0);
codecCtx_->max_b_frames = 0;
```

The `x264-params` string is the floor required for libx264 to be usable
over WebRTC.  Without it, the encoder produces frames that work on
loss-free LANs but freeze the receiver under any internet loss.

## Bitrate scaling

Bitrate is computed in `CameraSession::start` as a function of pixel rate:

```cpp
const long long pixelRate = w * h * fps;
long long bitrate = pixelRate / 10;     // ~0.1 bit/pixel/sec
bitrate = max(2 Mbps, min(12 Mbps, bitrate));
```

Same target for all three encoders.  How tightly each enforces it varies
(NVENC and VAAPI: tight; libx264: loose under default rate control), but
all three honour the order-of-magnitude.

## Known issues and pending work

The list below is roughly in benefit-to-cost order for the current
"single operator publishing to LiveKit" use case.  Each entry is
self-contained — pick whichever is most relevant for the next problem
you hit, not all of them at once.

### Add openh264 to the fallback chain

**What:** Add `h264_openh264` (Cisco's encoder) between VAAPI and
libx264 in the runtime fallback list.  The library already ships in our
AppImage as `libopenh264.so.8`; it just isn't probed.

**Cost:**
- **Code: small.**  Mirror `SoftwareEncoder` as `OpenH264Encoder`,
  call `avcodec_find_encoder_by_name("h264_openh264")` in the chain.
- **Quality at fixed bitrate: lower than libx264.**  openh264 is a
  conservative, WebRTC-aimed encoder; libx264 squeezes more detail out
  of the same bits.

**Benefit:**
- **Out-of-the-box WebRTC compatibility is much better than libx264.**
  openh264 emits MTU-sized slices, single-reference frames, and CBR
  rate control by default — the exact properties we have to
  manually override on libx264.  It's the encoder Chrome uses
  internally when you ask for "software H.264" in the browser.
- Useful as a fallback fallback: if a CPU-only host hits problems on
  libx264 even with our tuning, openh264 is one runtime fallback away.

**When to add:** If real-world testing on a CPU-only host shows
libx264-with-overrides isn't enough.  Cheap insurance.

### Switch libx264 to CBR rate control

**What:** Replace the implicit CRF rate control with explicit CBR via
`x264-params "nal-hrd=cbr:vbv-maxrate=N:vbv-bufsize=N"`.

**Cost:**
- **Quality at the same target bitrate: noticeably lower.**  libx264's
  CRF mode produces visibly better output for the same average rate.
- **Code: trivial** (extra `av_dict_set` on the existing options dict).

**Benefit:**
- **Frame-size variance drops to near zero** — every frame is roughly
  the same number of packets.  This is what NVENC and VAAPI already
  provide naturally and is the property the receiver's jitter buffer
  expects.

**When to add:** If libx264 is the encoder of record on a real
deployment.  Skip if libx264 is only a deep fallback.

### Throttle force-keyframe on libx264

**What:** When `requestKeyframe()` is called, ignore the request if
fewer than e.g. 250 ms have passed since the last forced IDR.

**Cost:**
- **Code: small.**  One `std::chrono::steady_clock::now()` comparison
  guarding the `pict_type = AV_PICTURE_TYPE_I` line in
  `SoftwareEncoder::encodeFrame`.

**Benefit:**
- **PLI flood from a struggling receiver no longer defeats
  `intra-refresh`.**  Today, the receiver under packet loss sends PLI
  → we force a big IDR → IDR fragments and loses too → receiver sends
  another PLI → infinite cycle of expensive IDRs.  Throttling
  force-keyframe lets `intra-refresh` actually do its job between
  PLIs.

**When to add:** If a libx264 deployment shows oscillating PLI / IDR
storms in webrtcbin debug logs.

### Encoder-level dynamic bitrate

**What:** Subscribe to webrtcbin's transport-cc feedback and adjust
each encoder's target bitrate at runtime.  See the matching section in
`webrtc-packet-recovery.md`.

**Cost:**
- **Code: moderate** (~100-200 lines across the encoder base class and
  each implementation).  NVENC and VAAPI accept live bitrate changes
  via `av_opt_set`; libx264 does too but is less reliable.
- **Determinism: lower** — bitrate is no longer a knob the developer
  controls directly.

**Benefit:**
- **Largest single "real internet" reliability win we don't yet
  have.**  When upload congests, the encoder backs off rather than
  dropping packets at the bottleneck.

**When to add:** If the deployment surface ever includes networks more
unpredictable than the test setup.

### VAAPI on NVIDIA via `nvidia-vaapi-driver`

**What:** NVIDIA has a libva shim that exposes NVDEC/NVENC through the
VAAPI API.  Today NVENC is probed first, so on NVIDIA hosts the
VAAPI path is never reached.

**Cost:** zero — already covered by the existing chain.

**Benefit:**
- Possible "VAAPI fallback" on machines where NVENC is intentionally
  disabled (e.g. enterprise environments that ship without CUDA libs
  but have nvidia-vaapi-driver).

**When to add:** Not actively needed.  Document for future
debuggability.

### Hardware capability probe instead of compile-time probe

**What:** Today `createEncoder` only checks `avcodec_find_encoder_by_name`
— whether the codec is *compiled in* to FFmpeg.  Whether it actually
*works* on this host is only discovered when `initialize()` fails at
runtime.

A more aggressive probe would do a "tiny encode" at startup (open a
codec context with minimal config, encode one black frame, close) to
distinguish "compiled in but not usable" up front, before pipeline
start.

**Cost:**
- **Code: moderate.**  Each encoder needs a "probe" path that's
  side-effect-free.
- **Startup time: +50-200 ms per probed encoder.**

**Benefit:**
- Removes the awkward "first session start may take an extra second
  while we discover the right encoder" behaviour.
- Useful for an eventual UI that wants to show the user which
  encoders are available before they hit Start.

**When to add:** When/if the camera-selection UI grows an "encoder"
chooser too.

## Diagnostic checklist

When the publish path is broken, the first thing to check is which
encoder actually got selected.  Look for:

```
[Encoder] selected: ...
```

If the line says `Software (libx264)`, every WebRTC quirk discussed in
this doc applies.  If it says one of the hardware paths, the bitstream
should be cleanly WebRTC-shaped and the freeze cause is more likely in
the network or the receiver.

For a one-shot encoder probe outside of session, run any small program
linked against `video_core` that calls `createEncoder` and prints the
result — easier than starting a full session.

## Cross-references

- `video-core/src/encode/encoder.cpp` — `createEncoder` factory and chain
- `video-core/src/encode/{nvenc,vaapi,software}_encoder.cpp` —
  per-encoder implementation
- `video-core/src/pipeline/video_pipeline.cpp` — runtime fallback chain
  in `VideoPipeline::start`
- `client/src/session/camera_session.cpp` — bitrate / framerate config
  building
- `docs/development/webrtc-packet-recovery.md` — NACK / RTX
  configuration; complements the encoder choices documented here
