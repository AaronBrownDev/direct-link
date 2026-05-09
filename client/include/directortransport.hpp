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
        // Latency measurement is filtered to a single publishing participant
        // because the matcher's ts-offset is only valid within one sender's
        // clock domain.  Pass the identity of the participant whose stream is
        // currently the main preview; pass an empty string to suspend
        // measurement (no active camera).  Switching resets the matcher and
        // emits a blanked breakdown so the UI doesn't display a stale value
        // attributed to the new camera.
        Q_INVOKABLE void setActiveParticipant(const QString &identity);
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
        // Periodic per-frame attribution of where the time inside video_lag
        // is being spent.  Sampled from libwebrtc getStats() on a 1 Hz
        // cadence so values update slower than latencyBreakdown but lag
        // much less than the EWMA in the matcher.
        //   jitterBufferMs   — mean ms each emitted frame waited in the
        //                      director-side jitter buffer
        //   decodeMs         — mean H.264 decode time per frame
        //   networkJitterMs  — RFC inter-arrival jitter on the inbound RTP
        //   framesPerSecond  — decoded fps over the polling interval
        // The "upstream" portion of video_lag (everything before the
        // director-side receiver — camera encode, WHIP, Ingress, SFU) is
        // computed in QML as: video_lag - jitter_buffer - decode.
        void videoStatsBreakdown(double jitterBufferMs, double decodeMs,
                                 double networkJitterMs, double framesPerSecond);
        // Emitted once when the first decoded frame with valid dimensions arrives.
        void videoResolutionChanged(int width, int height);

    private:
        Q_SLOT void onFrameArrived(qint64 receivedSteadyNs, qint64 frameTimestampUs,
                                   const QString &participantIdentity);
        // Bookkeeping slot for swap events; runs on the main thread.  The swap
        // timestamp itself is captured on the render thread (Qt::DirectConnection
        // lambda installed by setWindow()) and delivered here as a parameter.
        Q_SLOT void onFrameSwapped(qint64 swapSteadyNs);
        // Filtered per-track stats forwarded from DirectorSession.  Drops
        // samples from any participant other than the active main preview
        // (same reason the matcher does — keeps the breakdown consistent
        // with whatever frame is on screen).
        Q_SLOT void onVideoStats(double jitterBufferMs, double decodeMs,
                                 double networkJitterMs, double framesPerSecond,
                                 const QString &participantIdentity);

        // Reset matcher state and clear the capture queue.  Called from
        // setActiveParticipant() when the user switches main preview, and on
        // initial connection.  Also emits a blanked breakdown so the UI drops
        // any value attributed to the previous camera while the new camera's
        // matcher seeds.
        void resetLatencyMatcher();

        std::unique_ptr<livekit::Room> m_room;
        QString m_connection_state = "disconnected";
        std::unique_ptr<DirectorSession> m_session;
        gsl::owner<QFutureWatcher<bool> *> m_connectWatcher = nullptr;
        std::atomic<qint64> m_clock_offset_ns{0};
        // Identity of the publishing participant whose stream is currently
        // the main preview.  Read from both the DC packet path
        // (onUserPacketReceived, livekit-internal thread) and the frame path
        // (onFrameArrived, Qt main thread).  Mutations happen on the Qt main
        // thread via setActiveParticipant().  The mutex is uncontended in
        // steady state and the critical section is a QString compare/copy.
        mutable std::mutex m_active_participant_mutex;
        QString m_active_participant_identity;

        // Capture timestamps queued by onUserPacketReceived and consumed by
        // onFrameArrived.  Two arrival timestamps because dc_one_way needs a
        // wall-clock value (offset-corrected against the camera's wall clock)
        // while video_lag needs a director-local interval that survives an
        // NTP step.
        struct CaptureEntry {
            qint64 capture_ns;           // server-domain wall ns from camera
            qint64 dc_arrived_wall_ns;   // director system_clock — for dc_one_way only
            qint64 dc_arrived_steady_ns; // director steady_clock — for matching + video_lag
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

        // After this many consecutive ts-match failures, clear the seeded
        // offset so the next frame re-seeds.  Without this, a desync (e.g.
        // brief encoder stall, system clock jump, queue cap eviction of the
        // matching entry) latches the matcher into permanent misses and the
        // UI freezes on the last good sample.  ~30 frames at 30 fps is one
        // second of misses — long enough that transient stalls don't reset,
        // short enough that the user sees the display recover.
        static constexpr std::uint64_t MAX_CONSECUTIVE_MISSES_BEFORE_RESEED = 30;

        // After a matcher reset (camera switch), the first matches pair the
        // arriving frame against a DC near the warmup-target offset (~1 s
        // old) regardless of the new camera's true video_lag.  The TS-path
        // offset and EST-path estimated_video_lag then converge toward truth
        // via EWMA, but at the steady-state α=1/8 that takes long enough to
        // be visible on screen as a slow walk from ~1000 ms down to whatever
        // the new camera actually has.  Two coupled tweaks fix the visible
        // walk:
        //  1. Suppress emits for SETTLING_SAMPLES matches after the reset so
        //     the UI stays blank instead of showing the convergence walk.
        //  2. Use SETTLING_EWMA_DIVISOR (smaller divisor = larger α) on the
        //     EST and TS EWMAs while settling so they reach their fixed
        //     point inside the suppression window, then drop back to the
        //     stable steady-state divisor.
        static constexpr std::uint64_t SETTLING_SAMPLES = 30;
        static constexpr qint64 SETTLING_EWMA_DIVISOR = 2;
        static constexpr qint64 STEADY_EWMA_DIVISOR = 8;

        // Diagnostic counters (no synchronization — both modified from DC and
        // frame paths but only used for log throttling, not measurement).
        std::uint64_t m_dc_count = 0;
        std::uint64_t m_frame_count = 0;
        std::uint64_t m_ts_match_hits = 0;
        std::uint64_t m_ts_match_misses = 0;
        std::uint64_t m_ts_consecutive_misses = 0;
        std::uint64_t m_fifo_fallbacks = 0;
        // Counts down from SETTLING_SAMPLES after each matcher reset.  While
        // non-zero, emits are suppressed (UI stays blank) and the EWMA divisor
        // is the smaller SETTLING_EWMA_DIVISOR so the offset/estimate
        // converges quickly inside the suppression window.
        std::uint64_t m_settling_samples_remaining = 0;

        // Display gap: time from decoded frame available → QQuickWindow swap.
        // Sampled via frameSwapped; rolling average used as correction factor.
        // Both timestamps are steady_clock (director-local interval, never
        // crosses a machine boundary).
        QQuickWindow *m_window = nullptr;
        qint64 m_last_received_steady_ns = 0;
        bool m_frame_pending = false;
        static constexpr int DISPLAY_GAP_SAMPLES = 30;
        std::array<qint64, DISPLAY_GAP_SAMPLES> m_display_gap_buf{};
        int m_display_gap_idx = 0;
        int m_display_gap_count = 0;
        qint64 m_display_gap_sum_ns = 0;
};
