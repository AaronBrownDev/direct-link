# VideoOutput 4-Quadrant Artifact Debug

Continuation of the "ongoing issue" from `whip-publisher-debug.md`.

**Status: RESOLVED**

---

## Problem

The director's camera feed displayed as a 2x2 grid of identical sub-images.
Top two quadrants: magenta tint. Bottom two: green tint. Flashing a bright
light caused the top/bottom colors to swap. The main underlying image was
**black and white** (grayscale) — the color information was entirely wrong.

The operator preview looked **normal** — no artifact.

---

## Root cause

**NVENCEncoder pixel format mismatch** in
`video-core/src/encode/nvenc_encoder.cpp`.

The `createEncoder()` factory in `encoder.cpp` auto-upgrades to NVENC
when `h264_nvenc` is available on the system. The NVENC encoder was
configured with:

```cpp
codecCtx_->pix_fmt = AV_PIX_FMT_NV12; // line 52
```

But the camera capture pipeline always produces **YUV420P** (I420) frames.
The `encodeFrame()` method only converted input when it was NOT YUV420P:

```cpp
if (frame->format != AV_PIX_FMT_YUV420P || frame->linesize[0] == 0) {
    // convert to YUV420P...
}
```

Since the input was already YUV420P, no conversion occurred. The I420 data
was sent directly to NVENC, which interpreted it as NV12. Both formats have
the same Y plane, but their chroma layouts differ:

| Format | Chroma layout | Chroma stride (1280w) |
|--------|---------------|----------------------|
| I420 (YUV420P) | Separate U (W/2 x H/2) and V (W/2 x H/2) planes | 640 |
| NV12 | Interleaved UV (W x H/2) plane | 1280 |

NVENC read the I420 chroma data (stride 640) as NV12 (expecting stride
1280), causing the chroma to be sampled at half the expected width and
producing the 2x2 tiling artifact with scrambled colors.

---

## Fix

Two changes in `video-core/src/encode/nvenc_encoder.cpp`:

1. Changed `codecCtx_->pix_fmt` from `AV_PIX_FMT_NV12` to
   `AV_PIX_FMT_YUV420P` to match the camera capture output format.

2. Fixed the `encodeFrame()` conversion check to compare against
   `codecCtx_->pix_fmt` instead of hardcoding `AV_PIX_FMT_YUV420P`,
   and to also check for dimension mismatches.

---

## Why it was hard to find

The bug was obscured by several layers of indirection:

1. **The operator preview was correct** — it displays raw camera frames
   before encoding, so the NVENC bug was never visible locally.

2. **The encoder factory silently upgrades to NVENC** — `createEncoder()`
   in `encoder.cpp` probes for `h264_nvenc` and auto-upgrades from
   Software to Hardware. All debugging effort initially targeted
   `SoftwareEncoder`, which was never used.

3. **The artifact manifested at the decoder** — the corrupted H.264
   bitstream decoded to corrupted I420 on the director side, making
   it appear as a decoder/SDK bug rather than an encoder bug.

4. **The profile mismatch was a red herring** — ffprobe showed Main
   profile instead of Constrained Baseline, which consumed investigation
   time. The actual issue was the NV12/I420 mismatch, not the profile.

---

## Investigation timeline

| # | What was tested | Result | What it proved |
|---|-----------------|--------|----------------|
| 1 | QQuickPaintedItem bypass | Artifact persists | Not a Qt rendering bug |
| 2 | SDK RGBA conversion | Artifact in PPM dump | SDK data already corrupted |
| 3 | Manual I420->RGBA via planeInfos | Artifact persists | Not a planeInfos bug |
| 4 | Raw I420 dump in ffplay | Artifact present | Decoded I420 data is bad |
| 5 | NV12 interpretation | Worse (4x4 grid) | Not NV12 mislabeled as I420 |
| 6 | Direct I420->Qt YUV420P delivery | Artifact persists | Not a conversion bug |
| 7 | planeInfos vs data() diff | Identical | Data layout is consistent |
| 8 | H.264 bitstream dump | Artifact in ffplay | Bitstream itself is bad |
| 9 | Encoder input dump (30 frames) | All correct | Camera capture is fine |
| 10 | Standalone ffmpeg encode of same input | Correct | Our encoder API usage is the bug |
| 11 | Profile fix via AVDictionary | Still Main profile | SoftwareEncoder not used |
| 12 | NVENC pix_fmt fix (NV12 -> YUV420P) | **Fixed** | Root cause confirmed |

The turning point was attempt #11: adding debug logging to
`SoftwareEncoder::initialize()` produced no output, revealing that
`NVENCEncoder` was being used instead. Inspecting `nvenc_encoder.cpp`
immediately revealed the NV12/YUV420P mismatch.
