#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include "livekit/livekit.h"
#include "sessionclient.hpp"
#include "camera_latency_sender.hpp"

// Usage: ./build/test_latency_sender [--server <signaling_url>]
// Default: http://localhost:50051  (matches docker-compose.prod.yaml)
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    QCommandLineParser parser;
    parser.addOption({"server", "Signaling server URL", "url", "http://localhost:50051"});
    parser.process(app);

    SessionClient       client;
    CameraLatencySender sender;

    const QString director_id = "test-director";
    const QString camera_id   = "test-camera-latency";
    QString room_code;
    QString data_token;
    QString livekit_url;
    bool    is_connected = false;
    bool    close_state  = false;

    QEventLoop creation_loop;
    QEventLoop d_join_loop;
    QEventLoop c_join_loop;
    QEventLoop connect_loop;
    QEventLoop stream_loop;
    QEventLoop close_loop;

    client.connectToServer(QUrl(parser.value("server")));

    QObject::connect(&client, &SessionClient::error, &app, [](const QString &msg) {
        qCritical() << "[Test] Signaling error:" << msg;
    });

    // Create session
    QObject::connect(&client, &SessionClient::sessionCreated, &creation_loop, [&](const QString &c) {
        room_code = c;
        creation_loop.quit();
    });
    qDebug() << "[Test] Creating session...";
    client.createSession(director_id, 1);
    creation_loop.exec();
    Q_ASSERT(!room_code.isEmpty());
    qDebug() << "[Test] Session created:" << room_code;

    // Join as director so the LiveKit room exists before the camera joins
    QObject::connect(&client, &SessionClient::directorJoined, &d_join_loop,
        [&](const QString &, const QString &) { d_join_loop.quit(); });
    qDebug() << "[Test] Joining as director...";
    client.joinSession(room_code, director_id, "director");
    d_join_loop.exec();
    qDebug() << "[Test] Director joined.";

    // Join as camera to get dataToken + livekitUrl
    QObject::connect(&client, &SessionClient::cameraJoined, &c_join_loop,
        [&](const QString &, const QString &, const QString &dt, const QString &lu) {
            data_token  = dt;
            livekit_url = lu;
            c_join_loop.quit();
        });
    qDebug() << "[Test] Joining as camera...";
    client.joinSession(room_code, camera_id, "camera");
    c_join_loop.exec();
    Q_ASSERT(!data_token.isEmpty());
    Q_ASSERT(!livekit_url.isEmpty());
    qDebug() << "[Test] Camera joined. LiveKit URL:" << livekit_url;

    // Connect CameraLatencySender
    QObject::connect(&sender, &CameraLatencySender::connected, &connect_loop, [&]() {
        is_connected = true;
        connect_loop.quit();
    });
    QObject::connect(&sender, &CameraLatencySender::connectionFailed, &connect_loop, [&]() {
        connect_loop.quit();
    });

    qDebug() << "[Test] Starting CameraLatencySender...";
    sender.start(data_token, livekit_url);
    QTimer::singleShot(15000, &connect_loop, &QEventLoop::quit);
    connect_loop.exec();

    if (!is_connected) {
        qCritical() << "[Test] CameraLatencySender failed to connect within timeout.";
        QEventLoop abort_close;
        QObject::connect(&client, &SessionClient::sessionClosed, &abort_close,
            [&](bool) { abort_close.quit(); });
        client.closeSession(room_code, director_id);
        abort_close.exec();
        livekit::shutdown();
        return 1;
    }
    qDebug() << "[Test] CameraLatencySender connected. Streaming for 5 seconds...";

    QTimer::singleShot(5000, &stream_loop, &QEventLoop::quit);
    stream_loop.exec();

    sender.stop();
    qDebug() << "[Test] CameraLatencySender stopped.";

    // Close session
    QObject::connect(&client, &SessionClient::sessionClosed, &close_loop, [&](bool success) {
        close_state = success;
        close_loop.quit();
    });
    qDebug() << "[Test] Closing session...";
    client.closeSession(room_code, director_id);
    close_loop.exec();
    Q_ASSERT(close_state);

    qDebug() << "[Test] Success.";
    livekit::shutdown();
    return 0;
}
