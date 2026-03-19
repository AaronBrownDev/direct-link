#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "livekit/livekit.h"
#include "include/directortransport.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    int result;
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("application", "Main");
        result = app.exec();
    }

    DirectorTransport::instance()->shutdown();
    livekit::shutdown();

    return result;
}
