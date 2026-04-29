/*
 * File: camera_latency_sender.hpp
 * Author: Justin Williams
 * Date: 4/28/26
 * File Description: Connects to a LiveKit room as a data-only participant and
 * periodically publishes clock-offset-corrected capture timestamps on the
 * "latency" topic so the director can measure end-to-end camera-to-display
 * latency via the DirectorTransport data packet callback.
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

#include <atomic>
#include <memory>
#include <gsl/pointers>

#include <livekit/room.h>

class CameraLatencySender : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit CameraLatencySender(QObject *parent = nullptr);
    ~CameraLatencySender() override;

    CameraLatencySender(const CameraLatencySender &) = delete;
    CameraLatencySender &operator=(const CameraLatencySender &) = delete;
    CameraLatencySender(CameraLatencySender &&) = delete;
    CameraLatencySender &operator=(CameraLatencySender &&) = delete;

    static gsl::owner<CameraLatencySender *> create(QQmlEngine *engine, QJSEngine * /*unused*/) {
        return new CameraLatencySender(engine);
    }

    Q_INVOKABLE void start(const QString &token, const QString &url);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setClockOffset(qint64 offsetNs);

    // Called by CameraSessionController for each captured frame. Switches from
    // the 33ms fallback timer to frame-accurate timestamps on first call.
    Q_INVOKABLE void onFrameCaptured(qint64 captureNs);

signals:
    void connected();
    void connectionFailed();

private:
    void sendTimestamp();               // fallback timer path: uses now()
    void sendTimestampNs(qint64 serverNs); // common send path

    std::unique_ptr<livekit::Room> m_room;
    std::atomic<qint64> m_clock_offset_ns{0};
    QTimer *m_timer = nullptr;
    QFutureWatcher<bool> *m_connectWatcher = nullptr;
    bool m_frame_driven = false; // true after first onFrameCaptured call
};
