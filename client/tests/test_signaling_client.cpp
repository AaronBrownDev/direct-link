#include <QCoreApplication>
#include <QEventLoop>
#include <QDebug>
#include <QCommandLineParser>
#include "sessionclient.hpp"


int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    SessionClient client;

    QString user_id_director = "director-1";
    QString user_id_camera = "camera-1";
    int max_cameras = 4;
    QString room_code;
    QString token;
    QString livekit_url;
    QString whip_url;
    QString stream_key;
    bool close_state = false;

    QEventLoop creation_loop;
    QEventLoop d_join_loop;
    QEventLoop c_join_loop;
    QEventLoop close_loop;

    QCommandLineParser parser;
    parser.addOption({"server", "Signaling server URL", "url", "http://34.174.71.83:50051"});
    parser.process(app);
    client.connectToServer(QUrl(parser.value("server")));

    // ------------------------------------
    // ERROR HANDLER
    // ------------------------------------

    QObject::connect(&client, &SessionClient::error, &app, [&](const QString &msg) {
        qCritical() << "[Test] A signaling error occurred:" << msg;
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

    qDebug() << "[Test] Success.";

    return 0;
}

