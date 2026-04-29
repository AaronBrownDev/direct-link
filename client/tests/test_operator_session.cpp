#include <QCoreApplication>
#include <QEventLoop>
#include <QDebug>
#include <QTimer>
#include <QCommandLineParser>
#include <gst/gst.h>
#include "sessionclient.hpp"
#include "session_controller.hpp"

// ----------------------------------------
// Usage: ./build/test_operator_session [--server <signaling_url>]
// Default signaling url: http://34.174.71.83:50051
// ----------------------------------------
int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addOption({"server", "Signaling server URL", "url", "http://34.174.71.83:50051"});
    parser.process(app);

    const QUrl server_url(parser.value("server"));

    SessionClient client;
    CameraSessionController cam_session_control = CameraSessionController();

    QString user_id_director = "director-1";
    QString user_id_camera = "camera-1";
    int max_cameras = 1;
    QString room_code;
    QString token;
    QString livekit_url;
    QString stream_key;
    QString whip_url;
    bool is_cam_running = false;
    bool close_state = false;

    QEventLoop creation_loop;
    QEventLoop d_join_loop;
    QEventLoop c_join_loop;
    QEventLoop cam_start_loop;
    QEventLoop cam_stop_loop;
    QEventLoop close_loop;

    client.connectToServer(server_url);

    // ------------------------------------
    // ERROR HANDLER
    // ------------------------------------

    QObject::connect(&client, &SessionClient::error, &app, [&](const QString &msg) {
        qCritical() << "[Test] A signaling error occurred:" << msg;
    });

    QObject::connect(&cam_session_control, &CameraSessionController::errorOccurred, &app, [&](const QString &msg) {
        qCritical() << "[Test] A camera session error occurred:" << msg;
    });

    // ------------------------------------
    // SESSION CREATION
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionCreated, &creation_loop, [&](const QString &c) {
        room_code = c;
        creation_loop.quit();
    });

    qDebug() << "[Test] Creating Session...";

    client.createSession(user_id_director, max_cameras);
    creation_loop.exec();

    Q_ASSERT(!room_code.isEmpty());

    qDebug() << "[Test] Session Created.";

    // ------------------------------------
    // DIRECTOR SESSION JOIN
    // ------------------------------------

    QObject::connect(&client, &SessionClient::directorJoined, &d_join_loop, [&](const QString &t, const QString &l) {
        token = t;
        livekit_url = l;
        d_join_loop.quit();
    });

    qDebug() << "[Test] Joining Director...";
    client.joinSession(room_code, user_id_director, "director");
    d_join_loop.exec();

    Q_ASSERT(!token.isEmpty());
    Q_ASSERT(!livekit_url.isEmpty());

    qDebug() << "[Test] Director joined.";
    
    // ------------------------------------
    // OPERATOR SESSION JOIN
    // ------------------------------------

    QObject::connect(&client, &SessionClient::cameraJoined, &c_join_loop,
        [&](const QString &w, const QString &s, const QString &, const QString &) {
            whip_url = w;
            stream_key = s;
            c_join_loop.quit();
        });

    qDebug() << "[Test] Joining Operator...";
    client.joinSession(room_code, user_id_camera, "camera");
    c_join_loop.exec();

    Q_ASSERT(!whip_url.isEmpty());
    Q_ASSERT(!stream_key.isEmpty());

    qDebug() << "[Test] Operator joined.";

    // ------------------------------------
    // CAMERA SESSION START
    // ------------------------------------

    QObject::connect(&cam_session_control, &CameraSessionController::sessionStarted, &cam_start_loop, [&]() {
        is_cam_running = true;
        cam_start_loop.quit();
    });

    qDebug() << "[Test] Starting Camera Session...";
    cam_session_control.start(whip_url, stream_key);
    
    // 25-second timeout
    QTimer::singleShot(25000, &cam_start_loop, &QEventLoop::quit);
    cam_start_loop.exec();

    if (!is_cam_running) {
        qCritical() << "[Test] Timed out waiting for camera session";
        client.closeSession(room_code, user_id_director);
        QEventLoop abort_close;
        QObject::connect(&client, &SessionClient::sessionClosed, &abort_close,
                         [&](bool) { abort_close.quit(); });
        abort_close.exec();
        gst_deinit();
        return 1;
    }

    qDebug() << "[Test] Camera Session started.";

    // ------------------------------------
    // STREAM
    // ------------------------------------

    QEventLoop stream_loop;
    QTimer::singleShot(10000, &stream_loop, &QEventLoop::quit);
    stream_loop.exec();

    // ------------------------------------
    // CAMERA SESSION STOP
    // ------------------------------------

    QObject::connect(&cam_session_control, &CameraSessionController::sessionStopped, &cam_stop_loop, [&]() {
        is_cam_running = false;
        cam_stop_loop.quit();
    });

    qDebug() << "[Test] Stopping Camera Session...";
    cam_session_control.stop();
    cam_stop_loop.exec();

    Q_ASSERT(!is_cam_running);

    qDebug() << "[Test] Camera Session Stopped.";

    // ------------------------------------
    // SESSION CLOSING
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionClosed, &close_loop, [&](bool success) {
        close_state = success;
        close_loop.quit();
    });

    qDebug() << "[Test] Closing Session...";
    client.closeSession(room_code, user_id_director);
    close_loop.exec();

    Q_ASSERT(close_state);    

    qDebug() << "[Test] Session closed.";

    // ------------------------------------

    qDebug() << "[Test] Success.";
    return 0;
}