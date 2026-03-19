#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "livekit/livekit.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    QQmlApplicationEngine engine;

    int result = app.exec();

    engine.loadFromModule("application", "Main");

    return result;
}
