#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "framereader.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    engine.addImportPath("qrc:/");

    FrameReader frame_reader;
    engine.rootContext()->setContextProperty("FrameReader", &frame_reader);

    const QUrl url(QStringLiteral("qrc:/src/application/Main.qml"));

    engine.load(url);


    return app.exec();
}
