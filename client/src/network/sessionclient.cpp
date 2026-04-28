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

#include <chrono>

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
            qWarning() << "[SessionClient] CreateSession failed:" << status.message();
            emit error(status.message());
            return;
        }

        auto resp = reply->read<CreateSessionReply>();
        if (!resp) {
            emit error("Could not deserialize CreateSessionReply");
            return;
        }

        QString room_code = resp->roomCode();
        qDebug() << "[SessionClient] Room code:" << room_code;

        emit sessionCreated(room_code);
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
            qWarning() << "[SessionClient] JoinSession failed:" << status.message();
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

            qDebug() << "[SessionClient] Director joined.";
            qDebug() << "Token:" << token.left(40) << "...";
            qDebug() << "LiveKit URL:" << livekit_url;
            emit directorJoined(token, livekit_url);
        }
        else if (!resp->whipUrl().isEmpty()) {
            // Camera
            QString whip_url = resp->whipUrl();
            QString stream_key = resp->streamKey();

            qDebug() << "[SessionClient] Operator joined.";
            qDebug() << "WHIP URL:" << whip_url;
            qDebug() << "Stream key:" << stream_key;
            emit cameraJoined(whip_url, stream_key);
        }
        else {
            qWarning() << "[SessionClient] JoinReply had no credentials for either role";
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
            qWarning() << "[SessionClient] CloseSession failed:" << status.message();
            emit sessionClosed(false);
            return;
        }

        auto resp = reply->read<CloseSessionReply>();
        if (resp && resp->success()) {
            qDebug() << "[SessionClient] Session closed.";
            emit sessionClosed(true);
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

        QVariantList sessions;

        for (const auto &session : resp->sessions()) {
            qDebug() << session.roomCode()
                     << session.status()
                     << session.maxCameras();

            QVariantMap entry;
            entry["roomCode"] = session.roomCode();
            entry["roomStatus"] = session.status();
            entry["maxCameras"] = static_cast<int>(session.maxCameras());
            entry["createdAt"] = session.createdAt();
            sessions.append(entry);
        }

        emit sessionsReceived(sessions);
    });
}

void SessionClient::measureLatency() {
    GetServerTimeRequest req;

    const auto t1_steady = std::chrono::steady_clock::now();
    const qint64 t1_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto reply = m_client.GetServerTime(req);
    auto *reply_ptr = reply.get();

    QObject::connect(reply_ptr, &QGrpcCallReply::finished, this,
        [this, reply = std::move(reply), t1_steady, t1_ns](const QGrpcStatus &status) {
            if (!status.isOk()) {
                qWarning() << "[SessionClient] GetServerTime failed:" << status.message();
                return;
            }
            auto resp = reply->read<GetServerTimeReply>();
            if (!resp) {
                qWarning() << "[SessionClient] Could not deserialize GetServerTimeReply";
                return;
            }

            const auto t2_steady = std::chrono::steady_clock::now();
            const double rtt_ms = std::chrono::duration<double, std::milli>(
                t2_steady - t1_steady).count();

            // Approximate t2 wall-clock time by adding the steady-clock elapsed to t1,
            // avoiding a second system_clock::now() that could be affected by clock jumps.
            const qint64 t2_ns = t1_ns + std::chrono::duration_cast<std::chrono::nanoseconds>(
                t2_steady - t1_steady).count();

            // Clock offset formula: server_time - (t1 + t2) / 2
            const qint64 sample = resp->serverTimeNs() - ((t1_ns + t2_ns) / 2);

            if (m_offset_initialized) {
                m_clock_offset_ns = static_cast<qint64>(
                    (OFFSET_EMA_ALPHA * static_cast<double>(sample))
                    + ((1.0 - OFFSET_EMA_ALPHA) * static_cast<double>(m_clock_offset_ns)));
            } else {
                m_clock_offset_ns = sample;
                m_offset_initialized = true;
            }

            emit latencyMeasured(rtt_ms);
            emit clockOffsetChanged();
        }
    );
}
