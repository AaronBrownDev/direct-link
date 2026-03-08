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
    auto *reply_ptr = reply.get();

    QObject::connect(reply_ptr, &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
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

        QString roomCode = resp->roomCode();
        qDebug() << "Room code:" << roomCode;

        emit sessionCreated(resp->roomCode());
    });
}

void SessionClient::joinSession(const QString &roomCode, const QString &userId, const QString &role) {
    JoinRequest req;
    req.setRoomCode(roomCode);
    req.setUserId(userId);
    req.setRole(role);

    auto reply = m_client.JoinSession(req);
    auto *reply_ptr = reply.get();

    QObject::connect(reply_ptr, &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
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
            QString livekitUrl = resp->livekitUrl();

            qDebug() << "Token:" << token.left(40) << "...";
            qDebug() << "LiveKit URL:" << livekitUrl;
            emit directorJoined(token, livekitUrl);
        }
        else if (!resp->whipUrl().isEmpty()) {
            // Camera
            QString whipUrl = resp->whipUrl();
            QString streamKey = resp->streamKey();

            qDebug() << "WHIP URL:" << whipUrl;
            qDebug() << "Stream key:" << streamKey;
            emit cameraJoined(whipUrl, streamKey);
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
    auto *reply_ptr = reply.get();

    QObject::connect(reply_ptr, &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
        if (!status.isOk()) {
            qWarning() << "CloseSession failed:" << status.message();
            emit error(status.message());
            return;
        }

        auto resp = reply->read<CloseSessionReply>();
        if (resp && resp->success()) {
            qDebug() << "Session closed";
            emit sessionClosed();
        }
    });
}

void SessionClient::getMySessions(const QString &userId) {
    GetMySessionsRequest req;
    req.setUserId(userId);

    auto reply = m_client.GetMySessions(req);
    auto *reply_ptr = reply.get();

    QObject::connect(reply_ptr, &QGrpcCallReply::finished, this, [this, reply = std::move(reply)](const QGrpcStatus &status) {
        if (!status.isOk()) {
            emit error(status.message());
            return;
        }

        auto resp = reply->read<GetMySessionsReply>();
        if (!resp) {
            emit error("Could not deserialize FetchSessionReply");
            return;
        }

        QStringList roomCodes;

        for (const auto &session : resp->sessions()) {
            qDebug() << session.roomCode()
                     << session.status()
                     << session.maxCameras();
            roomCodes << session.roomCode();
        }

        emit sessionsReceived(roomCodes);
    });
}
