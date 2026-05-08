#include "../../include/capture/camera_enumerator.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace videoCore::capture {

namespace {

// Read a single (num, den) framerate fraction.
bool fractionFromValue(const GValue *value, int &num, int &den) {
    if (value == nullptr) {
        return false;
    }
    if (GST_VALUE_HOLDS_FRACTION(value)) {
        num = gst_value_get_fraction_numerator(value);
        den = gst_value_get_fraction_denominator(value);
        return den > 0;
    }
    return false;
}

// Append every framerate fraction encoded in `value` to `out`.  GStreamer caps
// expose framerates as either:
//   - a single fraction
//   - a list of fractions
//   - a fraction range
// For ranges we only emit the numeric upper bound and lower bound — the camera
// is unlikely to produce arbitrary fractional rates between them, but those
// two bounds give callers something usable.
void collectFramerates(const GValue *value,
                       std::vector<std::pair<int, int>> &out) {
    if (value == nullptr) {
        return;
    }
    int num = 0;
    int den = 1;
    if (fractionFromValue(value, num, den)) {
        out.emplace_back(num, den);
        return;
    }
    if (GST_VALUE_HOLDS_LIST(value)) {
        const guint n = gst_value_list_get_size(value);
        for (guint i = 0; i < n; ++i) {
            const GValue *item = gst_value_list_get_value(value, i);
            if (fractionFromValue(item, num, den)) {
                out.emplace_back(num, den);
            }
        }
        return;
    }
    if (GST_VALUE_HOLDS_FRACTION_RANGE(value)) {
        const GValue *min = gst_value_get_fraction_range_min(value);
        const GValue *max = gst_value_get_fraction_range_max(value);
        if (fractionFromValue(min, num, den)) {
            out.emplace_back(num, den);
        }
        if (fractionFromValue(max, num, den)) {
            out.emplace_back(num, den);
        }
    }
}

// Parse one GstCaps structure into one or more CameraFormat entries (one per
// distinct framerate).  Skips entries with non-integer width/height (caps
// ranges) since callers need a concrete resolution to drive the pipeline.
void appendFormatsFromStructure(const GstStructure *structure,
                                std::vector<CameraFormat> &out) {
    if (structure == nullptr) {
        return;
    }
    const gchar *media_type = gst_structure_get_name(structure);
    if (media_type == nullptr) {
        return;
    }

    gint width = 0;
    gint height = 0;
    if (!gst_structure_get_int(structure, "width", &width) ||
        !gst_structure_get_int(structure, "height", &height) || width <= 0 ||
        height <= 0) {
        // Range entry — skip.  Real cameras advertise discrete sizes.
        return;
    }

    std::vector<std::pair<int, int>> framerates;
    const GValue *fr_value = gst_structure_get_value(structure, "framerate");
    collectFramerates(fr_value, framerates);

    if (framerates.empty()) {
        // No framerate field at all is unusual but possible — treat as 30/1
        // so the caller has something to drive the pipeline with.
        framerates.emplace_back(30, 1);
    }

    for (const auto &[num, den] : framerates) {
        CameraFormat f;
        f.mediaType = media_type;
        f.width = width;
        f.height = height;
        f.framerateNum = num;
        f.framerateDen = den;
        out.push_back(std::move(f));
    }
}

// Pull a string property from GstStructure via either "device.path" (v4l2) or
// a fallback alternative key.  Returns empty string when neither is present.
std::string getStringProperty(const GstStructure *props, const char *key) {
    if (props == nullptr || key == nullptr) {
        return {};
    }
    const gchar *str = gst_structure_get_string(props, key);
    return str != nullptr ? std::string(str) : std::string();
}

CameraDevice fromGstDevice(GstDevice *device) {
    CameraDevice out;
    if (device == nullptr) {
        return out;
    }

    gchar *display = gst_device_get_display_name(device);
    if (display != nullptr) {
        out.displayName = display;
        g_free(display);
    }

    GstStructure *props = gst_device_get_properties(device);
    if (props != nullptr) {
        // v4l2: device.path = "/dev/video0".
        std::string v4l2_path = getStringProperty(props, "device.path");
        std::string api = getStringProperty(props, "device.api");

        if (!v4l2_path.empty()) {
            out.id = v4l2_path;
            out.source = "v4l2";
        }
        else if (api == "pipewire") {
            // pipewiresrc accepts target-object as either the node serial or
            // node.name.  serial is the stable identifier across launches.
            std::string serial = getStringProperty(props, "object.serial");
            if (serial.empty()) {
                serial = getStringProperty(props, "node.name");
            }
            out.id = serial;
            out.source = "pipewire";
        }
        else {
            // Unknown source — leave empty; caller will skip auto-selection.
            out.source = api;
        }
        gst_structure_free(props);
    }

    GstCaps *caps = gst_device_get_caps(device);
    if (caps != nullptr) {
        const guint n = gst_caps_get_size(caps);
        for (guint i = 0; i < n; ++i) {
            appendFormatsFromStructure(gst_caps_get_structure(caps, i),
                                       out.formats);
        }
        gst_caps_unref(caps);
    }

    return out;
}

} // namespace

std::vector<CameraDevice> CameraEnumerator::listDevices() {
    // gst_init is idempotent — safe to call from anywhere we touch GStreamer.
    if (gst_is_initialized() == FALSE) {
        gst_init(nullptr, nullptr);
    }

    std::vector<CameraDevice> result;

    GstDeviceMonitor *monitor = gst_device_monitor_new();
    if (monitor == nullptr) {
        return result;
    }

    // Filter to Video/Source so we don't pick up audio devices or sinks.
    // Empty caps = "any caps" so each device contributes its full advertised
    // format set.
    gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);

    if (gst_device_monitor_start(monitor) == FALSE) {
        gst_object_unref(monitor);
        return result;
    }

    GList *devices = gst_device_monitor_get_devices(monitor);
    for (GList *l = devices; l != nullptr; l = l->next) {
        auto *device = static_cast<GstDevice *>(l->data);
        CameraDevice info = fromGstDevice(device);
        // Drop anonymous entries — we can't drive a pipeline without an id.
        if (!info.id.empty()) {
            result.push_back(std::move(info));
        }
    }
    g_list_free_full(devices, gst_object_unref);

    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    return result;
}

std::optional<CameraFormat>
CameraEnumerator::pickBestFormat(const std::vector<CameraFormat> &formats,
                                 int preferredWidth, int preferredHeight,
                                 int preferredFps) {
    if (formats.empty()) {
        return std::nullopt;
    }

    auto pixels = [](const CameraFormat &f) {
        return static_cast<long long>(f.width) * f.height;
    };
    const long long preferred_pixels =
        static_cast<long long>(preferredWidth) * preferredHeight;
    const double preferred_fps = static_cast<double>(preferredFps);

    // Pass 1: exact match on (w, h, fps); prefer image/jpeg over raw because
    // MJPEG yields more headroom on USB-bandwidth-limited webcams.
    auto exactMatchScore = [&](const CameraFormat &f) -> int {
        if (f.width != preferredWidth || f.height != preferredHeight) {
            return -1;
        }
        if (std::abs(f.fps() - preferred_fps) > 0.5) {
            return -1;
        }
        return f.mediaType == "image/jpeg" ? 2 : 1;
    };

    const CameraFormat *best = nullptr;
    int best_score = 0;
    for (const auto &f : formats) {
        const int s = exactMatchScore(f);
        if (s > best_score) {
            best_score = s;
            best = &f;
        }
    }
    if (best != nullptr) {
        return *best;
    }

    // Pass 2: highest resolution still capable of >= preferredFps.
    best = nullptr;
    long long best_pixels = 0;
    for (const auto &f : formats) {
        if (f.fps() + 0.5 < preferred_fps) {
            continue;
        }
        const long long p = pixels(f);
        if (p > best_pixels) {
            best_pixels = p;
            best = &f;
        }
    }
    if (best != nullptr) {
        return *best;
    }

    // Pass 3: closest (by pixel count) to preferred resolution.
    best = nullptr;
    long long best_diff = 0;
    for (const auto &f : formats) {
        const long long diff = std::abs(pixels(f) - preferred_pixels);
        if (best == nullptr || diff < best_diff) {
            best_diff = diff;
            best = &f;
        }
    }
    return best != nullptr ? std::optional<CameraFormat>(*best) : std::nullopt;
}

std::optional<CameraDevice> CameraEnumerator::pickDefaultDevice() {
    auto devices = listDevices();
    if (devices.empty()) {
        return std::nullopt;
    }

    // Prefer v4l2 devices that actually advertise formats.  Fall back to any
    // v4l2 device, then any device.
    for (const auto &d : devices) {
        if (d.source == "v4l2" && !d.formats.empty()) {
            return d;
        }
    }
    for (const auto &d : devices) {
        if (d.source == "v4l2") {
            return d;
        }
    }
    return devices.front();
}

} // namespace videoCore::capture
