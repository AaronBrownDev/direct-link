#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <gstreamer-1.0/gst/gst.h>

#include "livekit/livekit.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
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
