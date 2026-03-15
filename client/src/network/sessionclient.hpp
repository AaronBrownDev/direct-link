/*
 * File: sessionclient.hpp
 * Author: Justin Williams
 * Date: 3/9/26
 * File Description: Wraps gRPC stubs to expose session controls to Qt.
 * Qt can invoke session creation, session joining, and active session
 * retrieval. Components can receive the response through signals.
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QtGrpc/QGrpcHttp2Channel>
#include "signaling_client.grpc.qpb.h"
#include "signaling.qpb.h"



class SessionClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit SessionClient(QObject *parent = nullptr);

    static SessionClient *create(QQmlEngine *, QJSEngine *) {
        static SessionClient instance;
        return &instance;
    }

    Q_INVOKABLE void connectToServer(const QUrl &url);

    Q_INVOKABLE void createSession(const QString &userId, int maxCameras);
    Q_INVOKABLE void joinSession(const QString &roomCode, const QString &userId, const QString &role);
    Q_INVOKABLE void closeSession(const QString &roomCode, const QString &userId);
    Q_INVOKABLE void getMySessions(const QString &userId);

signals:

    void sessionCreated(const QString &roomCode);

    void directorJoined(const QString &token, const QString &livekitUrl);
    void cameraJoined(const QString &whipUrl, const QString &streamKey);

    void sessionClosed(bool success);

    void sessionsReceived(const QVariantList &roomCodes);

    void error(const QString &message);

private:
    directlink::signaling::SignalingService::Client m_client;
};

