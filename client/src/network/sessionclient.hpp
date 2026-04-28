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

    // Rolling EMA of clock offset relative to server (nanoseconds).
    // corrected_local_time = QDateTime::currentMSecsSinceEpoch() * 1e6 + clockOffsetNs
    Q_PROPERTY(qint64 clockOffsetNs READ clockOffsetNs NOTIFY clockOffsetChanged)

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
    Q_INVOKABLE void measureLatency();

    [[nodiscard]] qint64 clockOffsetNs() const { return m_clock_offset_ns; }

signals:

    void sessionCreated(const QString &roomCode);
    void latencyMeasured(double rttMs);
    void clockOffsetChanged();

    void directorJoined(const QString &token, const QString &livekitUrl);
    void cameraJoined(const QString &whipUrl, const QString &streamKey);

    void sessionClosed(bool success);

    void sessionsReceived(const QVariantList &roomCodes);

    void error(const QString &message);

private:
    static constexpr double OFFSET_EMA_ALPHA = 0.25;

    directlink::signaling::SignalingService::Client m_client;
    qint64 m_clock_offset_ns = 0;
    bool m_offset_initialized = false;
};

