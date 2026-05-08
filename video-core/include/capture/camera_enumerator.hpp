#pragma once

#include <optional>
#include <string>
#include <vector>

namespace videoCore::capture {

// One supported (mediaType, resolution, framerate) tuple advertised by a
// camera.  v4l2 devices typically expose entries for each of "image/jpeg" and
// "video/x-raw" with one-or-more discrete framerates per resolution.
struct CameraFormat {
    std::string mediaType;  // "image/jpeg" or "video/x-raw"
    int width = 0;
    int height = 0;
    int framerateNum = 0;
    int framerateDen = 1;

    [[nodiscard]] double fps() const {
        return framerateDen > 0
                   ? static_cast<double>(framerateNum) / framerateDen
                   : 0.0;
    }
};

struct CameraDevice {
    // Stable identifier consumable by GstParse pipelines.  For v4l2 this is
    // the device path ("/dev/video0"); for pipewiresrc this is the
    // serial / object path the source's "target-object" property accepts.
    std::string id;
    std::string displayName;       // human-readable label
    std::string source;            // "v4l2" or "pipewire"
    std::vector<CameraFormat> formats;
};

class CameraEnumerator {
public:
    // Enumerate all Video/Source devices currently visible to GStreamer.
    // Safe to call repeatedly; each invocation creates and tears down its own
    // GstDeviceMonitor.  Devices without parseable formats are still returned
    // (with an empty formats vector) so the caller can show them but skip
    // auto-selection.
    [[nodiscard]] static std::vector<CameraDevice> listDevices();

    // Select the best format from a candidate list using a simple preference:
    //   1. Exact match for preferred (w, h, fps), prefer image/jpeg over raw
    //      (smaller USB bandwidth → higher achievable FPS on real webcams).
    //   2. Highest resolution still capable of at least preferredFps.
    //   3. Highest framerate at preferredWidth × preferredHeight.
    //   4. Highest pixel-count regardless of fps.
    // Returns nullopt only if `formats` is empty.
    [[nodiscard]] static std::optional<CameraFormat> pickBestFormat(
        const std::vector<CameraFormat> &formats, int preferredWidth = 1280,
        int preferredHeight = 720, int preferredFps = 30);

    // First device whose formats list is non-empty (preferred) or, failing
    // that, first device returned by the monitor.  Returns nullopt if none.
    [[nodiscard]] static std::optional<CameraDevice> pickDefaultDevice();
};

} // namespace videoCore::capture
