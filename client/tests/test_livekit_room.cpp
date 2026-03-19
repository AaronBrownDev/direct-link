#include <QCoreApplication>
#include <QEventLoop>
#include <QDebug>
#include <QTimer>
#include "sessionclient.hpp"
#include "directortransport.hpp"

#include "livekit/livekit.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    SessionClient client;
    DirectorTransport *transport = DirectorTransport::instance();

    QString user_id_director = "director-1";
    int max_cameras = 1;
    QString room_code;
    QString token;
    QString livekit_url;
    bool isConnected = false;
    bool close_state = false;

    QEventLoop creation_loop;
    QEventLoop d_join_loop;
    QEventLoop room_connect_loop;
    QEventLoop close_loop;

    client.connectToServer(QUrl("http://localhost:50051"));

    // ------------------------------------
    // ERROR HANDLER
    // ------------------------------------

    QObject::connect(&client, &SessionClient::error, &app, [&](const QString &msg) {
        qCritical() << "An error occurred:" << msg;
    });

    // ------------------------------------
    // SESSION CREATION
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionCreated, &creation_loop, [&](const QString &c) {
        room_code = c;
        creation_loop.quit();
    });

    qDebug() << "Creating Session...";

    client.createSession(user_id_director, max_cameras);
    creation_loop.exec();

    Q_ASSERT(!room_code.isEmpty());

    qDebug() << "Session Created.";

    // ------------------------------------
    // SESSION JOIN
    // ------------------------------------

    QObject::connect(&client, &SessionClient::directorJoined, &d_join_loop, [&](const QString &t, const QString &l) {
        token = t;
        livekit_url = l;
        d_join_loop.quit();
    });

    qDebug() << "Joining Director...";
    client.joinSession(room_code, user_id_director, "director");
    d_join_loop.exec();

    Q_ASSERT(!token.isEmpty());
    Q_ASSERT(!livekit_url.isEmpty());

    qDebug() << "Director joined.";

    // ------------------------------------
    // LIVEKIT CONNECT
    // ------------------------------------

    QObject::connect(transport, &DirectorTransport::connected, &room_connect_loop, [&]() {
        isConnected = true;
        room_connect_loop.quit();
    });

    qDebug() << "Connecting...";
    transport->connectToRoom(token, livekit_url);
    // 10-second timeout
    QTimer::singleShot(10000, &room_connect_loop, &QEventLoop::quit);
    room_connect_loop.exec();

    if (!isConnected) {
        qWarning() << "Timed out waiting for connection.";
        client.closeSession(room_code, user_id_director);
        return 1;
    }

    qDebug() << "Director connected.";

    // ------------------------------------
    // LIVEKIT DISCONNECT
    // ------------------------------------

    QObject::connect(transport, &DirectorTransport::disconnected, &app, [&]() {
        isConnected = false;
    });

    qDebug() << "Disconnecting...";
    transport->disconnectFromRoom();

    Q_ASSERT(!isConnected);

    qDebug() << "Director disconnected.";

    // ------------------------------------
    // SESSION CLOSING
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionClosed, &close_loop, [&](bool success) {
        close_state = success;
        close_loop.quit();
    });

    qDebug() << "Closing Session...";
    client.closeSession(room_code, user_id_director);
    close_loop.exec();

    Q_ASSERT(close_state);

    qDebug() << "Success.";

    livekit::shutdown();
    return 0;
}