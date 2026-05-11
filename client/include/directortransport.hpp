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

        // Current best estimate of video_lag for warmup-gating and seeding
        // the ts_us offset.  Returns libwebrtc's last reported JB delay
        // when available — that's a measurement of the actual playout-side
        // wait time, accurate within a few tens of ms of true video_lag in
        // steady state — and falls back to INITIAL_VIDEO_LAG_GUESS_NS only
        // for the first seed of the session (no stats yet) or if stats
        // haven't been populated.  Used at reseed time so a transient
        // jitter burst that triggers MAX_CONSECUTIVE_MISSES doesn't snap
        // the offset back to the 1 s warmup guess in steady state.
        [[nodiscard]] qint64 currentVideoLagGuessNs() const;

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
        // worth of DCs at the current arrival rate.  120 entries covers ~4 s
        // at 30 DC/s — comfortable margin over the ~1-2 s libwebrtc warmup
        // video_lag, with headroom for the convergence burst where the
        // matcher consumes DCs at the same rate they arrive (one-in-one-out
        // per frame, no over-drain) so the queue does not shrink below the
        // in-flight pipeline depth.
        static constexpr std::size_t MAX_CAPTURE_QUEUE_SIZE = 120;

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
        // True iff the current seed was computed against the
        // INITIAL_VIDEO_LAG_GUESS_NS warmup constant (i.e. no libwebrtc JB
        // stat had arrived yet at seed time).  Such a seed is biased by
        // ~1 s; once enough JB stats arrive to confirm the buffer has
        // warmed up, onVideoStats forces a single reseed against the real
        // (converged) JB number.  Without this flag, transient JB=0
        // readings during pipeline freezes would re-trigger the recovery
        // on every pause-resume cycle and drift the offset.
        bool m_seeded_with_warmup_guess = false;
        // Count of non-zero JB stats seen since the last warmup-guess
        // seed.  The forcing reseed waits until this reaches
        // JB_STATS_BEFORE_RESEED so we don't reseed against the very
        // first stat (which can be ~10-50 ms while the buffer is still
        // ramping up) and lock in an under-counted offset.
        std::uint32_t m_jb_stats_since_warmup_seed = 0;
        static constexpr std::uint32_t JB_STATS_BEFORE_RESEED = 3;
        // Total non-zero JB stats observed this session.  The early stats
        // can be ~10-50 ms while libwebrtc's buffer is still ramping up
        // toward its converged target; seeding against one of those
        // produces a permanently biased matcher (since EWMA refinement
        // is intentionally disabled).  currentVideoLagGuessNs falls back
        // to the warmup constant until this passes MIN_JB_SAMPLES_FOR_GUESS,
        // so the initial seed uses the warmup-guess path even when JB has
        // technically published a value already.  Reseed via the existing
        // m_seeded_with_warmup_guess path then locks the offset against a
        // stable JB reading.
        std::uint32_t m_jb_samples_seen = 0;
        static constexpr std::uint32_t MIN_JB_SAMPLES_FOR_GUESS = 3;
        // Acceptable mismatch between predicted and found capture_ns.  A
        // single network-jitter spike on the GKE WAN path can push the
        // matching DC's capture_ns 100–200 ms outside the prediction
        // window; with the previous 100 ms tolerance, 30 such frames in a
        // row triggered a reseed against the warmup guess and silently
        // biased the matcher.  250 ms is wide enough to ride out typical
        // jitter bursts while still being narrow enough that a structurally
        // wrong match (multi-frame offset shift) gets caught.
        static constexpr qint64 TS_MATCH_TOLERANCE_NS = 250'000'000;
        // Heuristic seed for the very FIRST ts_us-valid frame of a session,
        // before any libwebrtc stats are available.  ~1 s covers the
        // ingress jitter buffer floor during warmup.  After matching has
        // produced at least one libwebrtc JB stat reading, the reseed path
        // uses that stat instead (see currentVideoLagGuessNs).
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

        // Rolling diagnostics so steady-state convergence is visible without
        // scanning every per-sample line.  We keep the last ROLLING_WINDOW
        // frames worth of values for each component and emit a single
        // [DT-rolling] summary line every ROLLING_LOG_INTERVAL frames.
        //
        // video_lag_fifo_oldest_ms is logged in parallel with the ts_us
        // match's video_lag — a persistent gap between them is the smoking
        // gun for ts_us mismatch (matcher picking too-new DCs).
        static constexpr std::size_t ROLLING_WINDOW = 100;
        static constexpr std::uint64_t ROLLING_LOG_INTERVAL = 100;
        std::array<double, ROLLING_WINDOW> m_rolling_dc_ms{};
        std::array<double, ROLLING_WINDOW> m_rolling_video_lag_ts_ms{};
        std::array<double, ROLLING_WINDOW> m_rolling_video_lag_fifo_ms{};
        std::array<double, ROLLING_WINDOW> m_rolling_total_ms{};
        std::array<double, ROLLING_WINDOW> m_rolling_jb_ms{};
        std::array<double, ROLLING_WINDOW> m_rolling_decode_ms{};
        std::size_t m_rolling_idx = 0;
        std::size_t m_rolling_count = 0;
        // Latest video stats values, captured from onVideoStats and folded
        // into the rolling summary on the next breakdown emit.  This couples
        // the two log streams so the summary shows full attribution.
        double m_latest_jb_ms = 0.0;
        double m_latest_decode_ms = 0.0;

        // Camera-side per-packet send delay (pacer + NACK retransmit buffer)
        // reported by webrtcbin via the v2 DC payload's trailing uint32 ms
        // field.  Folded into currentVideoLagGuessNs alongside JB to capture
        // sender-side buffering that JB alone misses on lossy upstream paths.
        // 0 when the camera is on an old client (8-byte payload) or no
        // valid stat has landed yet.  Written from the livekit packet thread
        // (onUserPacketReceived); read from the matcher path.
        std::atomic<double> m_latest_sender_delay_ms{0.0};
};
