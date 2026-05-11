#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <gstreamer-1.0/gst/gst.h>
#include <cstring>
#include <iostream>

#include "livekit/livekit.h"
#include "../../video-core/include/common/latency_overlay.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // --benchmark-latency turns on the ground-truth overlay (camera-side
    // draws a 128x128 black/white timestamp pattern in the top-left corner
    // of each frame; director-side decodes it for absolute end-to-end
    // latency).  Visible artifact in the output video — never use in
    // production.  See docs/development/latency-measurement.md.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--benchmark-latency") == 0) {
            videoCore::benchmark::setLatencyOverlayEnabled(true);
            std::cerr << "[main] --benchmark-latency: latency overlay enabled\n";
        }
    }

    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
    gst_init(nullptr, nullptr);

    int result = 0;
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("application", "Main");
        result = app.exec();
    }

    livekit::shutdown();
    gst_deinit();

    return result;
}
