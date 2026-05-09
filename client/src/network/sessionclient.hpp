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
#include <array>
#include <cstddef>
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
    void cameraJoined(const QString &whipUrl, const QString &streamKey,
                      const QString &dataToken, const QString &livekitUrl);

    void sessionClosed(bool success);

    void sessionsReceived(const QVariantList &roomCodes);

    void error(const QString &message);

private:
    // PTP-style minimum-RTT clock-offset tracking.  Each GetServerTime call
    // produces an (rtt, offset) sample where the offset is accurate to
    // roughly ±rtt/2 (Cristian's algorithm bound, assuming path symmetry).
    // A small ring buffer of recent samples is kept; the published offset
    // is the offset of the sample with the lowest RTT in the buffer.  No
    // EMA — averaging in higher-RTT samples only dilutes the best
    // measurement we have.  Window of 16 ≈ 16 s of one-per-second samples,
    // long enough to keep a representative low-RTT sample around but short
    // enough to track real clock drift.
    static constexpr std::size_t SYNC_WINDOW_SIZE = 16;

    struct SyncSample {
        double rtt_ms;
        qint64 offset_ns;
    };

    directlink::signaling::SignalingService::Client m_client;
    qint64 m_clock_offset_ns = 0;
    bool m_offset_initialized = false;
    std::array<SyncSample, SYNC_WINDOW_SIZE> m_sync_window{};
    std::size_t m_sync_window_count = 0;
    std::size_t m_sync_window_idx = 0;
};

