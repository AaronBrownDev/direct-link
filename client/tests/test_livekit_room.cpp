#include <QCoreApplication>
#include <QEventLoop>
#include <QDebug>
#include <QTimer>
#include <QCommandLineParser>
#include "sessionclient.hpp"
#include "directortransport.hpp"

#include "livekit/livekit.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    SessionClient client;
    DirectorTransport transport = DirectorTransport();

    QString user_id_director = "director-1";
    int max_cameras = 1;
    QString room_code;
    QString token;
    QString livekit_url;
    bool is_connected = false;
    bool close_state = false;

    QEventLoop creation_loop;
    QEventLoop d_join_loop;
    QEventLoop room_connect_loop;
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
    // SESSION JOIN
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
    // LIVEKIT CONNECT
    // ------------------------------------

    QObject::connect(&transport, &DirectorTransport::connected, &room_connect_loop, [&]() {
        is_connected = true;
        room_connect_loop.quit();
    });

    qDebug() << "[Test] Connecting...";
    transport.connectToRoom(token, livekit_url);
    // 10-second timeout
    QTimer::singleShot(10000, &room_connect_loop, &QEventLoop::quit);
    room_connect_loop.exec();

    if (!is_connected) {
        qWarning() << "[Test] Timed out waiting for connection.";
        client.closeSession(room_code, user_id_director);
        return 1;
    }

    qDebug() << "[Test] Director connected.";

    // ------------------------------------
    // LIVEKIT DISCONNECT
    // ------------------------------------

    // Synchronous: is_connected should already be false before Q_ASSERT if the disconnect succeeds
    QObject::connect(&transport, &DirectorTransport::disconnected, &app, [&]() {
        is_connected = false;
    });

    qDebug() << "[Test] Disconnecting...";
    transport.disconnectFromRoom();

    Q_ASSERT(!is_connected);

    qDebug() << "[Test] Director disconnected.";

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

    livekit::shutdown();
    return 0;
}