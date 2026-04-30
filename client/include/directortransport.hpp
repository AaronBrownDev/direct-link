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
#include <cstdint>
#include <deque>
#include <mutex>

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
        // Emitted once when the first decoded frame with valid dimensions arrives.
        void videoResolutionChanged(int width, int height);

    private:
        Q_SLOT void onFrameArrived(qint64 receivedNs, qint64 frameTimestampUs);
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
        std::deque<CaptureEntry> m_capture_queue;
        // The queue size sets the matching window: timestamp-based matching
        // needs the matching DC to still be in the queue when the frame
        // arrives, so the window must cover at least the actual video_lag
        // worth of DCs at the current arrival rate.  60 entries covers ~2 s
        // at 30 DC/s and ~6 s at 10 DC/s — both bound the realistic matching
        // window for typical and degraded WHIP/Ingress paths respectively.
        static constexpr std::size_t MAX_CAPTURE_QUEUE_SIZE = 60;

        // Timestamp-based matching: each VideoFrameEvent carries a ts_us field
        // (libwebrtc-aligned capture-time estimate, microseconds, in some
        // sender-correlated epoch).  Each DC carries the camera's std::chrono
        // capture_ns.  Both refer to the same physical moment for a given
        // frame, but live in different clock domains.  We learn the constant
        // offset between them and use it to match each frame to the right DC,
        // which avoids the rate-mismatch artifact that breaks FIFO matching
        // when DC and frame arrival rates differ.
        //
        // Relationship: capture_ns ≈ ts_us * 1000 - m_ts_capture_offset_ns
        qint64 m_ts_capture_offset_ns = 0;
        bool m_ts_offset_initialized = false;
        // Acceptable mismatch between predicted and found capture_ns.  Three
        // frame intervals at 30 fps ≈ 100 ms is generous early in stream
        // (when offset is freshly seeded) without being so wide that a wrong
        // match becomes possible.
        static constexpr qint64 TS_MATCH_TOLERANCE_NS = 100'000'000;
        // Heuristic seed for the very first ts_us-valid frame: the matching
        // DC arrived approximately this many ns ago.  ~1 s covers the ingress
        // jitter buffer floor; the EWMA self-corrects within a few frames.
        static constexpr qint64 INITIAL_VIDEO_LAG_GUESS_NS = 1'000'000'000;

        // Fallback when ts_us is never populated (docker prod loopback path).
        // We still need to pick a sensible DC for each frame instead of
        // FIFO-popping the oldest (which gives the queue-cap artifact).  The
        // approach: maintain a rolling estimate of video_lag and match each
        // frame to the DC whose age is closest to that estimate.  This gives
        // an approximate but stable measurement, useful for local dev.
        qint64 m_estimated_video_lag_ns = INITIAL_VIDEO_LAG_GUESS_NS;
        // Generous tolerance so the match isn't trapped at the seed value if
        // the real lag is well above or below 1 s.
        static constexpr qint64 ESTIMATED_LAG_TOLERANCE_NS = 500'000'000;

        // Diagnostic counters (no synchronization — both modified from DC and
        // frame paths but only used for log throttling, not measurement).
        std::uint64_t m_dc_count = 0;
        std::uint64_t m_frame_count = 0;
        std::uint64_t m_ts_match_hits = 0;
        std::uint64_t m_ts_match_misses = 0;
        std::uint64_t m_fifo_fallbacks = 0;

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
