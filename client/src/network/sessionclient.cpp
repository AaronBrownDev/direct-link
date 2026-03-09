/*
 * File: sessionclient.cpp
 * Author: Justin Williams
 * Date: 3/8/26
 * File Description: A class implementing a SessionClient wrapper. The
 * SessionClient exposes gRPC stub methods to Qt, allowing QML
 * files to invoke the methods. A session can be created, joined, and
 * closed. A list of a user's active sessions can also be retreived.
 */

#include <QDebug>
#include "sessionclient.hpp"

using namespace directlink::signaling;

SessionClient::SessionClient(QObject *parent) : QObject(parent) {}

void SessionClient::connectToServer(const QUrl &url) {
    auto channel = std::make_shared<QGrpcHttp2Channel>(url);

    m_client.attachChannel(channel);
}

void SessionClient::createSession(const QString &userId, int maxCameras) {
    CreateSessionRequest req;
    req.setUserId(userId);
    req.setMaxCameras(maxCameras);

    auto reply = m_client.CreateSession(req);

    QObject::connect(reply.get(), &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
        if (!status.isOk()) {
            qWarning() << "CreateSession failed:" << status.message();
            emit error(status.message());
            return;
        }

        auto resp = reply->read<CreateSessionReply>();
        if (!resp) {
            emit error("Could not deserialize CreateSessionReply");
            return;
        }

        QString room_code = resp->roomCode();
        qDebug() << "Room code:" << room_code;

        emit sessionCreated(room_code);
    });
}

void SessionClient::joinSession(const QString &roomCode, const QString &userId, const QString &role) {
    JoinRequest req;
    req.setRoomCode(roomCode);
    req.setUserId(userId);
    req.setRole(role);

    auto reply = m_client.JoinSession(req);

    QObject::connect(reply.get(), &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
        if (!status.isOk()) {
            qWarning() << "JoinSession failed:" << status.message();
            emit error(status.message());
            return;
        }

        auto resp = reply->read<JoinReply>();
        if (!resp) {
            emit error("Could not deserialize JoinSessionReply");
            return;
        }

        if (!resp->token().isEmpty()) {
            // Director
            QString token = resp->token();
            QString livekit_url = resp->livekitUrl();

            qDebug() << "Token:" << token.left(40) << "...";
            qDebug() << "LiveKit URL:" << livekit_url;
            emit directorJoined(token, livekit_url);
        }
        else if (!resp->whipUrl().isEmpty()) {
            // Camera
            QString whip_url = resp->whipUrl();
            QString stream_key = resp->streamKey();

            qDebug() << "WHIP URL:" << whip_url;
            qDebug() << "Stream key:" << stream_key;
            emit cameraJoined(whip_url, stream_key);
        }
        else {
            qWarning() << "JoinReply had no credentials for either role";
            emit error("Could not join session");
        }
    });
}

void SessionClient::closeSession(const QString &roomCode, const QString &userId) {
    CloseSessionRequest req;
    req.setRoomCode(roomCode);
    req.setUserId(userId);

    auto reply = m_client.CloseSession(req);

    QObject::connect(reply.get(), &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
        if (!status.isOk()) {
            qWarning() << "CloseSession failed:" << status.message();
            emit sessionClosed(false);
            return;
        }

        auto resp = reply->read<CloseSessionReply>();
        if (resp && resp->success()) {
            qDebug() << "Session closed.";
            emit sessionClosed(true);
        }
    });
}

void SessionClient::getMySessions(const QString &userId) {
    GetMySessionsRequest req;
    req.setUserId(userId);

    auto reply = m_client.GetMySessions(req);

    QObject::connect(reply.get(), &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
        if (!status.isOk()) {
            emit error(status.message());
            return;
        }

        auto resp = reply->read<GetMySessionsReply>();
        if (!resp) {
            emit error("Could not deserialize FetchSessionReply");
            return;
        }

        QStringList room_codes;

        for (const auto &session : resp->sessions()) {
            qDebug() << session.roomCode()
                     << session.status()
                     << session.maxCameras();
            room_codes << session.roomCode();
        }

        emit sessionsReceived(room_codes);
    });
}
