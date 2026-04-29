/*
 * File: directortransport.hpp
 * Author: Justin Williams
 * Date: 3/15/26
 * File Description: Accesses LiveKit's RoomDelegate events to manage a LiveKit room
 * and its tracks. The QML application can connect to a LiveKit room given a LiveKit token
 * and url. The class owns a DirectorSession that it exposes to QML as a property. It can 
 * attach and detach LiveKit tracks from its DirectorSession.
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>
#include <QQuickWindow>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <livekit/room_delegate.h>
#include <gsl/pointers>
#include <gsl/util>

#include <array>
#include <atomic>
#include <mutex>
#include <queue>

#include "directorsession.hpp"

class DirectorTransport : public QObject, public livekit::RoomDelegate {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(DirectorSession* session READ session NOTIFY sessionChanged)

    public:
        explicit DirectorTransport(QObject *parent = nullptr);
        ~DirectorTransport() override;

        DirectorTransport(const DirectorTransport &) = delete;
        DirectorTransport &operator=(const DirectorTransport &) = delete;
        DirectorTransport(DirectorTransport &&) = delete;
        DirectorTransport &operator=(DirectorTransport &&) = delete;

        static gsl::owner<DirectorTransport *>create(QQmlEngine *engine, QJSEngine * /*unused*/) {
            return new DirectorTransport(engine);
        }

        void onParticipantConnected(
            livekit::Room & /*unused*/,
            const livekit::ParticipantConnectedEvent &event) override;

        void onTrackSubscribed(
            livekit::Room & /*unused*/,
            const livekit::TrackSubscribedEvent &event) override;

        void onTrackSubscriptionFailed(
            livekit::Room & /*unused*/,
            const livekit::TrackSubscriptionFailedEvent &event) override;

        void onTrackUnsubscribed(
            livekit::Room & /*unused*/,
            const livekit::TrackUnsubscribedEvent &event) override;

        void onConnectionStateChanged(
            livekit::Room & /*unused*/,
            const livekit::ConnectionStateChangedEvent &event) override;

        void onDisconnected(
            livekit::Room & /*unused*/,
            const livekit::DisconnectedEvent &event) override;

        void onUserPacketReceived(
            livekit::Room & /*unused*/,
            const livekit::UserDataPacketEvent &event) override;

        [[nodiscard]] QString connectionState() const;
        [[nodiscard]] DirectorSession* session() const;

        Q_INVOKABLE void connectToRoom(const QString &token, const QString &url);
        Q_INVOKABLE void disconnectFromRoom();
        Q_INVOKABLE void shutdown();
        Q_INVOKABLE void setClockOffset(qint64 ns);
        Q_INVOKABLE void setWindow(QObject *window);
        [[nodiscard]] Q_INVOKABLE double displayGapMs() const;

    signals:
        void connected();
        void disconnected();
        void connectionStateChanged(const QString &newState);
        void sessionChanged();
        void latencyMeasured(double latencyMs);
        // dc_one_way_ms: camera preview callback → DC packet arrived at director
        // video_lag_ms:  DC packet arrived → video frame decoded  (jitter + decode)
        // display_gap_ms: video frame decoded → QQuickWindow swap
        void latencyBreakdown(double dcOneWayMs, double videoLagMs, double displayGapMs);

    private:
        Q_SLOT void onFrameArrived(qint64 receivedNs);
        Q_SLOT void onFrameSwapped();

        std::unique_ptr<livekit::Room> m_room;
        QString m_connection_state = "disconnected";
        std::unique_ptr<DirectorSession> m_session;
        gsl::owner<QFutureWatcher<bool> *> m_connectWatcher = nullptr;
        std::atomic<qint64> m_clock_offset_ns{0};

        // Capture timestamps (server clock, nanoseconds) queued by
        // onUserPacketReceived and consumed by onFrameArrived.
        struct CaptureEntry {
            qint64 capture_ns;    // server-clock timestamp sent by camera
            qint64 dc_arrived_ns; // director local wall-clock when DC packet arrived
        };

        std::mutex m_capture_queue_mutex;
        std::queue<CaptureEntry> m_capture_queue;
        static constexpr std::size_t MAX_CAPTURE_QUEUE_SIZE = 60; // ~2 s at 30 fps

        // Display gap: time from decoded frame available → QQuickWindow swap.
        // Sampled via frameSwapped; rolling average used as correction factor.
        QQuickWindow *m_window = nullptr;
        qint64 m_last_received_ns = 0;
        bool m_frame_pending = false;
        static constexpr int DISPLAY_GAP_SAMPLES = 30;
        std::array<qint64, DISPLAY_GAP_SAMPLES> m_display_gap_buf{};
        int m_display_gap_idx = 0;
        int m_display_gap_count = 0;
        qint64 m_display_gap_sum_ns = 0;
};
