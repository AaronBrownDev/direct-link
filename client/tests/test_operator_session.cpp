#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <gst/gst.h>

#include "session_controller.hpp"
#include "sessionclient.hpp"

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);

    const QString serverUrl = (app.arguments().size() > 1)
        ? app.arguments().at(1)
        : QStringLiteral("http://34.174.71.83:50051");

    SessionClient client;
    CameraSessionController controller;

    QString roomCode;
    QString whipUrl;
    QString streamKey;
    bool sessionStarted = false;

    QEventLoop createLoop;
    QEventLoop directorLoop;
    QEventLoop operatorLoop;
    QEventLoop startLoop;
    QEventLoop closeLoop;

    QObject::connect(&client, &SessionClient::error, &app, [](const QString &msg) {
        qCritical() << "[test] Signaling error:" << msg;
    });

    qDebug() << "[test] Connecting to" << serverUrl;
    client.connectToServer(QUrl(serverUrl));

    // ------------------------------------
    // SESSION CREATION
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionCreated, &createLoop,
                     [&](const QString &code) {
                         roomCode = code;
                         createLoop.quit();
                     });

    qDebug() << "[test] Creating session...";
    client.createSession("director-test", 1);
    createLoop.exec();

    Q_ASSERT(!roomCode.isEmpty());
    qDebug() << "[test] Session created:" << roomCode;

    // ------------------------------------
    // DIRECTOR JOIN (establishes the session on the server)
    // ------------------------------------

    QObject::connect(&client, &SessionClient::directorJoined, &directorLoop,
                     [&](const QString &, const QString &) {
                         directorLoop.quit();
                     });

    qDebug() << "[test] Director joining...";
    client.joinSession(roomCode, "director-test", "director");
    directorLoop.exec();

    qDebug() << "[test] Director joined.";

    // ------------------------------------
    // OPERATOR JOIN
    // ------------------------------------

    QObject::connect(&client, &SessionClient::cameraJoined, &operatorLoop,
                     [&](const QString &url, const QString &key) {
                         whipUrl = url;
                         streamKey = key;
                         operatorLoop.quit();
                     });

    qDebug() << "[test] Operator joining...";
    client.joinSession(roomCode, "operator-test", "camera");
    operatorLoop.exec();

    Q_ASSERT(!whipUrl.isEmpty());
    Q_ASSERT(!streamKey.isEmpty());
    qDebug() << "[test] Operator joined. WHIP URL:" << whipUrl;

    // ------------------------------------
    // CAMERA SESSION START
    // ------------------------------------

    QObject::connect(&controller, &CameraSessionController::sessionStarted,
                     &startLoop, [&]() {
                         sessionStarted = true;
                         startLoop.quit();
                     });

    QObject::connect(&controller, &CameraSessionController::errorOccurred,
                     &startLoop, [&](const QString &msg) {
                         qCritical() << "[test] Camera session error:" << msg;
                         startLoop.quit();
                     });

    qDebug() << "[test] Starting camera session...";
    controller.start(whipUrl, streamKey);

    // 25s — covers the 20s gst_element_get_state timeout plus startup
    QTimer::singleShot(25000, &startLoop, &QEventLoop::quit);
    startLoop.exec();

    if (!sessionStarted) {
        qCritical() << "[test] Camera session failed to start. Aborting.";
        client.closeSession(roomCode, "director-test");
        QEventLoop abortClose;
        QObject::connect(&client, &SessionClient::sessionClosed, &abortClose,
                         [&](bool) { abortClose.quit(); });
        abortClose.exec();
        gst_deinit();
        return 1;
    }

    qDebug() << "[test] Camera session started. Streaming for 10 seconds...";

    // ------------------------------------
    // STREAM
    // ------------------------------------

    QEventLoop streamLoop;
    QTimer::singleShot(10000, &streamLoop, &QEventLoop::quit);
    streamLoop.exec();

    // ------------------------------------
    // STOP
    // ------------------------------------

    qDebug() << "[test] Stopping camera session...";
    controller.stop();
    qDebug() << "[test] Camera session stopped.";

    // ------------------------------------
    // CLOSE SESSION
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionClosed, &closeLoop,
                     [&](bool success) {
                         if (!success) {
                             qWarning() << "[test] Session close reported failure.";
                         }
                         closeLoop.quit();
                     });

    qDebug() << "[test] Closing session...";
    client.closeSession(roomCode, "director-test");
    closeLoop.exec();

    qDebug() << "[test] Done.";
    gst_deinit();
    return 0;
}
