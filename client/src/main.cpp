#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "livekit/livekit.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    int result = 0;
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("application", "Main");
        result = app.exec();
    }

    livekit::shutdown();

    return result;
}
