#include "directortransport.hpp"

#include <QDebug>
#include <livekit/remote_participant.h>
#include <livekit/remote_track_publication.h>
#include <livekit/room.h>

#include <chrono>
#include <cstdlib>
#include <iterator>
#include <limits>

static std::string_view disconnectReasonToString(livekit::DisconnectReason reason) {
    switch (reason) {
        case livekit::DisconnectReason::ClientInitiated:    return "ClientInitiated";
        case livekit::DisconnectReason::ConnectionTimeout:  return "ConnectionTimeout";
        case livekit::DisconnectReason::DuplicateIdentity:  return "DuplicateIdentity";
        case livekit::DisconnectReason::JoinFailure:        return "JoinFailure";
        case livekit::DisconnectReason::MediaFailure:       return "MediaFailure";
        case livekit::DisconnectReason::Migration:          return "Migration";
        case livekit::DisconnectReason::ParticipantRemoved: return "ParticipantRemoved";
        case livekit::DisconnectReason::RoomClosed:         return "RoomClosed";
        case livekit::DisconnectReason::RoomDeleted:        return "RoomDeleted";
        case livekit::DisconnectReason::ServerShutdown:     return "ServerShutdown";
        case livekit::DisconnectReason::SignalClose:        return "SignalClose";
        case livekit::DisconnectReason::SipTrunkFailure:    return "SipTrunkFailure";
        case livekit::DisconnectReason::StateMismatch:      return "StateMismatch";
        case livekit::DisconnectReason::Unknown:            return "Unknown";
        case livekit::DisconnectReason::UserRejected:       return "UserRejected";
        case livekit::DisconnectReason::UserUnavailable:    return "UserUnavailable";
        default:                                            return "<unrecognised>";
    }
}

DirectorTransport::DirectorTransport(QObject *parent) : QObject(parent) {}

DirectorTransport::~DirectorTransport() {
    shutdown();
}

void DirectorTransport::onParticipantConnected(livekit::Room & /*unused*/, const livekit::ParticipantConnectedEvent &event) {
    qDebug() << "[DirectorTransport] Participant connected.\n\tidentity="
                << event.participant->identity()
                << "\n\tname="
                << event.participant->name() << "\n";
}

void DirectorTransport::onTrackSubscribed(livekit::Room & /*unused*/, const livekit::TrackSubscribedEvent &event) {
    const char *participant_id = (event.participant != nullptr) ? event.participant->identity().c_str() : "<unknown>";
    const std::string track_sid = event.publication ? event.publication->sid() : "<unknown>";
    const std::string track_name = event.publication ? event.publication->name() : "<unknown>";

    qDebug() << "[DirectorTransport] Track subscribed.\n\tparticipant_id=" << participant_id
             << "\n\ttrack_sid=" << track_sid
             << "\n\ttrack_name=" << track_name;

    if (event.track) {
        qDebug() << "kind=" << static_cast<int>(event.track->kind());
    }

    if (event.publication) {
        qDebug() << "source=" << static_cast<int>(event.publication->source());
    }

    if (event.track && event.track->kind() == livekit::TrackKind::KIND_VIDEO) {
        auto track = event.track;
        const QString identity = (event.participant != nullptr)
            ? QString::fromStdString(event.participant->identity())
            : QString();
        QMetaObject::invokeMethod(this, [this, track, track_sid, identity]() {
        if (m_session) {
            m_session->attachTrack(track, track_sid, identity);
        }
        }, Qt::QueuedConnection);

    }

}

void DirectorTransport::onTrackSubscriptionFailed(livekit::Room & /*unused*/, const livekit::TrackSubscriptionFailedEvent &event) {
    const char *participant_id = (event.participant != nullptr) ? event.participant->identity().c_str() : "<unknown>";
    const std::string msg = event.error;
    const std::string track_sid = event.track_sid;

    qWarning() << "[DirectorTransport] Failed to subscribe to track."
             << "\n\tparticipant_id=" << participant_id
             << "\n\ttrack_sid=" << track_sid
             << "\n\terror=" << msg;

    QMetaObject::invokeMethod(this, [this, track_sid]() {
        if (m_session) {
            m_session->detachTrack(track_sid);
        }
    }, Qt::QueuedConnection);
}

void DirectorTransport::onTrackUnsubscribed(livekit::Room & /*unused*/, const livekit::TrackUnsubscribedEvent &event) {
    const char *participant_id = (event.participant != nullptr) ? event.participant->identity().c_str() : "<unknown>";
    const std::string track_sid = event.publication ? event.publication->sid() : "<unknown>";
    const std::string track_name = event.publication ? event.publication->name() : "<unknown>";
    
    qDebug() << "[DirectorTransport] Track unsubscribed.\n\tparticipant_id=" << participant_id
             << "\n\ttrack_sid=" << track_sid
             << "\n\ttrack_name=" << track_name;

    if (event.track) {
        qDebug() << "kind=" << static_cast<int>(event.track->kind());
    }

    if (event.publication) {
        qDebug() << "source=" << static_cast<int>(event.publication->source());
    }

    QMetaObject::invokeMethod(this, [this, track_sid]() {
        if (m_session) {
            m_session->detachTrack(track_sid);
        }
    }, Qt::QueuedConnection);
}

void DirectorTransport::onConnectionStateChanged(livekit::Room & /*unused*/, const livekit::ConnectionStateChangedEvent &event) {
    livekit::ConnectionState state = event.state;

    QMetaObject::invokeMethod(this, [this, state]() {
        QString new_state;

        switch (state) {
        case livekit::ConnectionState::Connected:
            new_state = "connected";
            break;
        case livekit::ConnectionState::Reconnecting:
            new_state = "connecting";
            break;
        case livekit::ConnectionState::Disconnected:
            new_state = "disconnected";
            break;
        default:
            break;
    }

    if (m_connection_state == new_state) { return; }

    m_connection_state = new_state;
    qDebug() << "[DirectorTransport] Connection status changed.\n\tstatus=" << m_connection_state;
    emit connectionStateChanged(m_connection_state);
    }, Qt::QueuedConnection);

}

void DirectorTransport::onUserPacketReceived(livekit::Room & /*unused*/, const livekit::UserDataPacketEvent &event) {
    if (event.topic != "latency" || event.data.size() != 8) {
        return;
    }

    // Drop DC packets from any participant other than the active main
    // preview.  The matcher's ts-offset is only valid within one sender's
    // clock domain, so mixing DCs from multiple cameras would cause every
    // non-active camera's frame to miss the match window indefinitely.
    //
    // A single logical camera shows up as two LiveKit identities:
    //   - video publisher (WHIP Ingress participant) = <UserId>
    //   - DC publisher (camera-side SDK)             = <UserId>_data
    // (see backend/internal/signaling/grpc.go).  The active participant
    // identity tracked here is the video-publisher form (what onTrackSubscribed
    // surfaces and what QML's track list exposes), so DC senders match if
    // they're either that identity or its "_data" sibling.
    {
        std::lock_guard<std::mutex> lock(m_active_participant_mutex);
        if (m_active_participant_identity.isEmpty()) {
            return;
        }
        const QString sender_identity = (event.participant != nullptr)
            ? QString::fromStdString(event.participant->identity())
            : QString();
        const bool matches = sender_identity == m_active_participant_identity
            || sender_identity == m_active_participant_identity + "_data";
        if (!matches) {
            return;
        }
    }

    // Deserialize big-endian int64 — capture time in server clock domain.
    qint64 capture_ns = 0;
    for (int i = 0; i < 8; ++i) {
        capture_ns = (capture_ns << 8) | static_cast<qint64>(event.data[static_cast<std::size_t>(i)]);
    }

    // Stamp arrival in two clocks at the same instant.  Wall is used by
    // dc_one_way (offset-corrected against the camera's wall clock); steady
    // is used by the matcher and by video_lag so director-local intervals
    // don't fold in any NTP step on the system clock.
    const qint64 dc_arrived_wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const qint64 dc_arrived_steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Enqueue for consumption by onFrameArrived when the corresponding decoded
    // video frame arrives. Drop the oldest entry if the queue is full (e.g.
    // video pipeline stalled).
    std::size_t qsize_after = 0;
    bool dropped_oldest = false;
    {
        std::lock_guard<std::mutex> lock(m_capture_queue_mutex);
        if (m_capture_queue.size() >= MAX_CAPTURE_QUEUE_SIZE) {
            m_capture_queue.pop_front();
            dropped_oldest = true;
        }
        m_capture_queue.push_back({capture_ns, dc_arrived_wall_ns, dc_arrived_steady_ns});
        qsize_after = m_capture_queue.size();
    }

    // Diagnostic: log every Nth packet to see queue behavior without spam.
    ++m_dc_count;
    if (m_dc_count <= 5 || (m_dc_count % 30) == 0 || dropped_oldest) {
        qDebug().nospace()
            << "[DT-diag] DC#" << m_dc_count
            << " capture_ns=" << capture_ns
            << " dc_arrived_wall_ns=" << dc_arrived_wall_ns
            << " dc_arrived_steady_ns=" << dc_arrived_steady_ns
            << " qsize_after=" << qsize_after
            << (dropped_oldest ? " DROPPED_OLDEST" : "");
    }
}

void DirectorTransport::onFrameArrived(qint64 receivedSteadyNs, qint64 frameTimestampUs,
                                       const QString &participantIdentity) {
    // Always record for display-gap sampling even if no timestamp is queued.
    m_last_received_steady_ns = receivedSteadyNs;
    m_frame_pending = true;

    // Drop frames from any participant other than the active main preview
    // for the same reason as the DC filter: the matcher state lives in a
    // single sender's clock domain.  Display-gap sampling above is unaffected
    // because it only uses receivedNs and is per-frame, not per-camera.
    {
        std::lock_guard<std::mutex> lock(m_active_participant_mutex);
        if (m_active_participant_identity.isEmpty() ||
            participantIdentity != m_active_participant_identity) {
            return;
        }
    }

    CaptureEntry entry{};
    std::size_t qsize_before = 0;
    bool used_ts_match = false;
    qint64 ts_match_diff_ns = 0;
    {
        std::lock_guard<std::mutex> lock(m_capture_queue_mutex);
        qsize_before = m_capture_queue.size();
        if (m_capture_queue.empty()) {
            ++m_frame_count;
            qDebug().nospace()
                << "[DT-diag] FRAME#" << m_frame_count
                << " ts_us=" << frameTimestampUs
                << " received_steady_ns=" << receivedSteadyNs
                << " qsize=0 (no DC entry — skipped)";
            return;
        }

        // Warmup gate: after a matcher reset (initial activation or camera
        // switch), the first ~video_lag worth of frames have their matching
        // DCs not yet in the queue (those DCs were either pre-activation, so
        // filtered out, or pre-reset, so cleared).  Seeding the offset
        // against whatever shallow DC happens to be available locks in a
        // wrong offset that under-reports video_lag for the entire session.
        // Skip frames for matching until the queue spans the expected
        // pipeline depth — DCs keep accumulating in the meantime.
        if (!m_ts_offset_initialized && !m_capture_queue.empty()) {
            const qint64 oldest_dc_age = receivedSteadyNs - m_capture_queue.front().dc_arrived_steady_ns;
            if (oldest_dc_age < INITIAL_VIDEO_LAG_GUESS_NS) {
                ++m_frame_count;
                if (m_frame_count <= 3 || (m_frame_count % 30) == 0) {
                    qDebug().nospace()
                        << "[DT-diag] FRAME#" << m_frame_count
                        << " warmup: oldest_dc_age_ms="
                        << (oldest_dc_age / 1'000'000)
                        << " qsize=" << qsize_before
                        << " (waiting for queue to span video_lag)";
                }
                return;
            }
        }

        // Timestamp-based matching when ts_us is populated.  ts_us is 0 in
        // the first few frames before RTCP SR arrives at the receiver, so we
        // fall back to FIFO until libwebrtc fills it in.
        if (frameTimestampUs > 0) {
            // Seed the offset on first valid ts_us.  We don't know which DC
            // is the true match, but we can guess: the matching DC arrived
            // ~INITIAL_VIDEO_LAG_GUESS_NS ago.  Pick the queue entry whose
            // dc_arrived_ns is closest to (receivedNs - INITIAL_VIDEO_LAG_GUESS).
            // Compute offset from that pairing; the EWMA below will refine.
            if (!m_ts_offset_initialized) {
                const qint64 target_dc_arrived = receivedSteadyNs - INITIAL_VIDEO_LAG_GUESS_NS;
                auto seed = m_capture_queue.begin();
                qint64 seed_diff = std::numeric_limits<qint64>::max();
                for (auto it = m_capture_queue.begin(); it != m_capture_queue.end(); ++it) {
                    const qint64 d = std::abs(it->dc_arrived_steady_ns - target_dc_arrived);
                    if (d < seed_diff) { seed_diff = d; seed = it; }
                }
                m_ts_capture_offset_ns = frameTimestampUs * 1000 - seed->capture_ns;
                m_ts_offset_initialized = true;
                qDebug().nospace()
                    << "[DT-diag] ts_us offset seeded: offset_ns=" << m_ts_capture_offset_ns
                    << " from queue entry " << std::distance(m_capture_queue.begin(), seed)
                    << "/" << m_capture_queue.size()
                    << " (seed_dc_age_ms=" << ((receivedSteadyNs - seed->dc_arrived_steady_ns) / 1'000'000)
                    << ")";
            }

            // Find the DC whose capture_ns is closest to the predicted value
            // for this frame's ts_us.  Linear scan — queue is bounded by
            // MAX_CAPTURE_QUEUE_SIZE so this is O(queue_size) per frame.
            const qint64 target_capture_ns = frameTimestampUs * 1000 - m_ts_capture_offset_ns;
            auto best = m_capture_queue.end();
            qint64 best_diff = std::numeric_limits<qint64>::max();
            for (auto it = m_capture_queue.begin(); it != m_capture_queue.end(); ++it) {
                const qint64 d = std::abs(it->capture_ns - target_capture_ns);
                if (d < best_diff) { best_diff = d; best = it; }
            }

            if (best != m_capture_queue.end() && best_diff <= TS_MATCH_TOLERANCE_NS) {
                entry = *best;
                ts_match_diff_ns = best_diff;
                used_ts_match = true;
                ++m_ts_match_hits;
                m_ts_consecutive_misses = 0;
                // Drop everything up to and including the matched entry —
                // older DCs correspond to frames that came before this one
                // and are now orphaned (their frames either arrived earlier
                // or were dropped between sender and decoder).
                m_capture_queue.erase(m_capture_queue.begin(), std::next(best));
                // EWMA refinement of the offset.  The offset is naturally
                // ~1.78e18 ns (different epochs of capture_ns and ts_us*1000),
                // so the delta form is required — `prior * 7` would overflow
                // int64.  The delta itself is small (a few ms) for matches.
                // Use the larger settling-window step while convergence is
                // happening (post-reset), then drop to the steady divisor.
                const qint64 observed_offset = frameTimestampUs * 1000 - entry.capture_ns;
                const qint64 delta = observed_offset - m_ts_capture_offset_ns;
                const qint64 ewma_divisor = (m_settling_samples_remaining > 0)
                    ? SETTLING_EWMA_DIVISOR
                    : STEADY_EWMA_DIVISOR;
                m_ts_capture_offset_ns += delta / ewma_divisor;
            } else {
                // No DC within tolerance.  This frame's matching DC was
                // probably lost in transit, or our offset is still off after
                // a recent stall.  Don't pop the queue (a later frame may
                // still need an older DC); just skip the latency report.
                ++m_ts_match_misses;
                ++m_ts_consecutive_misses;
                ++m_frame_count;
                if (m_ts_match_misses <= 5 || (m_ts_match_misses % 30) == 0) {
                    qDebug().nospace()
                        << "[DT-diag] FRAME#" << m_frame_count
                        << " ts_us=" << frameTimestampUs
                        << " ts-match MISS#" << m_ts_match_misses
                        << " best_diff_ms=" << (best_diff / 1'000'000)
                        << " qsize=" << qsize_before
                        << " offset_ns=" << m_ts_capture_offset_ns;
                }
                // If we've been missing for a sustained period the offset is
                // desynced from the live pipeline (clock jump, encoder stall,
                // matching entry evicted at the queue cap).  Reseed it so the
                // next valid frame re-derives the offset from current data,
                // which unfreezes the breakdown signal for the UI.
                if (m_ts_consecutive_misses >= MAX_CONSECUTIVE_MISSES_BEFORE_RESEED) {
                    qDebug().nospace()
                        << "[DT-diag] ts offset reseed after "
                        << m_ts_consecutive_misses << " consecutive misses"
                        << " (qsize=" << qsize_before
                        << " last_offset_ns=" << m_ts_capture_offset_ns << ")";
                    m_ts_offset_initialized = false;
                    m_ts_consecutive_misses = 0;
                }
                return;
            }
        } else {
            // ts_us not populated (e.g. docker prod loopback path).  Pick
            // the DC whose age is closest to our running estimate of the
            // video pipeline latency, rather than FIFO-popping the oldest
            // (which is the queue-cap artifact we're trying to avoid).
            const qint64 target_dc_arrived = receivedSteadyNs - m_estimated_video_lag_ns;
            auto best = m_capture_queue.end();
            qint64 best_diff = std::numeric_limits<qint64>::max();
            for (auto it = m_capture_queue.begin(); it != m_capture_queue.end(); ++it) {
                const qint64 d = std::abs(it->dc_arrived_steady_ns - target_dc_arrived);
                if (d < best_diff) { best_diff = d; best = it; }
            }
            if (best != m_capture_queue.end() && best_diff <= ESTIMATED_LAG_TOLERANCE_NS) {
                entry = *best;
                ts_match_diff_ns = best_diff;
                // Erase up to and including the matched entry — older DCs
                // correspond to frames that came before this one.
                m_capture_queue.erase(m_capture_queue.begin(), std::next(best));
                ++m_fifo_fallbacks;
                // EWMA refinement: pull the estimate toward the observed lag.
                // Larger step inside the post-reset settling window.
                const qint64 observed_lag = receivedSteadyNs - entry.dc_arrived_steady_ns;
                const qint64 ewma_divisor = (m_settling_samples_remaining > 0)
                    ? SETTLING_EWMA_DIVISOR
                    : STEADY_EWMA_DIVISOR;
                m_estimated_video_lag_ns += (observed_lag - m_estimated_video_lag_ns) / ewma_divisor;
            } else {
                // No DC within tolerance.  Fall back to oldest-pop so the
                // measurement still emits something for this frame, but it
                // will be the queue-cap artifact value.
                entry = m_capture_queue.front();
                m_capture_queue.pop_front();
                ++m_fifo_fallbacks;
            }
        }
    }

    const qint64 offset = m_clock_offset_ns.load(std::memory_order_relaxed);

    // dc_one_way: camera preview callback → DC packet arrived at director.
    // Both sides expressed in server clock domain (capture_ns is server-domain
    // wall as sent by the camera; dc_arrived_wall_ns + director offset puts
    // the director's local wall into server domain), so the offset cancels
    // the camera↔director wall-clock skew exactly once.
    const double dc_one_way_ms = static_cast<double>(entry.dc_arrived_wall_ns + offset - entry.capture_ns) / 1e6;

    // video_lag: DC packet arrived → video frame decoded (encode pipeline +
    // jitter buffer + decode).  Both timestamps are steady_clock on the
    // director — the difference is unaffected by NTP corrections to the
    // system clock between the two reads.
    const double video_lag_ms = static_cast<double>(receivedSteadyNs - entry.dc_arrived_steady_ns) / 1e6;

    const double gap_ms = displayGapMs();
    const double total_ms = dc_one_way_ms + video_lag_ms + gap_ms;

    // Diagnostics: how stale is the DC entry we just popped?  This reveals
    // queue mismatch (entries piling up while frames are slow to arrive).
    //   popped_age_ms — time between popped DC's arrival at director and now
    //                   (≈ video_lag for this frame, modulo queue mismatch).
    //   ts_to_capture_skew — frame.timestamp_us (libwebrtc capture estimate,
    //                       sender domain) minus popped DC capture_ns/1000.
    //                       If frame & DC are correctly paired, this is a
    //                       small constant offset between the two clocks.
    //                       If varying wildly, FIFO is matching wrong frames.
    ++m_frame_count;
    const qint64 popped_age_ns = receivedSteadyNs - entry.dc_arrived_steady_ns;
    const qint64 ts_to_capture_skew_us =
        frameTimestampUs - (entry.capture_ns / 1000);
    if (m_frame_count <= 10 || (m_frame_count % 5) == 0) {
        qDebug().nospace()
            << "[DT-diag] FRAME#" << m_frame_count
            << (used_ts_match ? " [TS]" : " [FIFO]")
            << " ts_us=" << frameTimestampUs
            << " popped_capture_ns=" << entry.capture_ns
            << " popped_dc_arrived_steady_ns=" << entry.dc_arrived_steady_ns
            << " qsize_before_pop=" << qsize_before
            << " video_lag_ms=" << (static_cast<double>(popped_age_ns) / 1'000'000.0)
            << " ts_match_diff_ms=" << (ts_match_diff_ns / 1'000'000)
            << " hits=" << m_ts_match_hits
            << " misses=" << m_ts_match_misses
            << " fifo_fb=" << m_fifo_fallbacks;
    }

    qDebug() << "[DirectorTransport] Latency breakdown:"
             << "\n\tdc_one_way=" << dc_one_way_ms << "ms"
             << "\n\tvideo_lag=" << video_lag_ms << "ms"
             << "\n\tdisplay_gap=" << gap_ms << "ms"
             << "\n\ttotal=" << total_ms << "ms"
             << "\n\tclock_offset=" << (static_cast<double>(offset) / 1'000'000.0) << "ms"
             << "\n\tmatch=" << (used_ts_match ? "ts_us" : "fifo");

    // Settling window after a matcher reset: suppress emits while the EWMA
    // converges so the UI doesn't display the convergence walk (e.g. a slow
    // decrease from ~1000 ms toward the new camera's true lag of ~60 ms).
    // The matcher state above keeps updating, so by the time the window
    // closes the EWMAs have reached their fixed point at the larger
    // SETTLING_EWMA_DIVISOR step.  After settling, normal emits resume.
    if (m_settling_samples_remaining > 0) {
        --m_settling_samples_remaining;
        return;
    }

    // Always emit the breakdown when within sane absolute bounds, even when
    // dc_one_way is briefly negative (clock-sync jitter, especially under WSL
    // where the guest clock can step relative to the host between ClockSync
    // ticks).  Suppressing the signal on negative dc froze the UI on the last
    // good sample whenever the offset overshot.  latencyMeasured stays gated
    // to non-negative totals because consumers treat it as a single positive
    // health indicator, while the breakdown lets the UI surface each
    // component honestly.
    if (total_ms < 30000.0) {
        if (total_ms > 0.0) {
            emit latencyMeasured(total_ms);
        }
        emit latencyBreakdown(dc_one_way_ms, video_lag_ms, gap_ms);
    }
}

void DirectorTransport::onVideoStats(double jitterBufferMs, double decodeMs,
                                     double networkJitterMs, double framesPerSecond,
                                     const QString &participantIdentity) {
    {
        std::lock_guard<std::mutex> lock(m_active_participant_mutex);
        if (m_active_participant_identity.isEmpty() ||
            participantIdentity != m_active_participant_identity) {
            return;
        }
    }
    emit videoStatsBreakdown(jitterBufferMs, decodeMs, networkJitterMs, framesPerSecond);
}

void DirectorTransport::onFrameSwapped(qint64 swapSteadyNs) {
    if (!m_frame_pending) { return; }
    m_frame_pending = false;

    const qint64 gap_ns = swapSteadyNs - m_last_received_steady_ns;

    // Sanity bounds: ignore gaps outside 0–500 ms (stale frames, system hiccup).
    if (gap_ns <= 0 || gap_ns > 500'000'000LL) { return; }

    m_display_gap_sum_ns -= gsl::at(m_display_gap_buf, m_display_gap_idx);
    gsl::at(m_display_gap_buf, m_display_gap_idx) = gap_ns;
    m_display_gap_sum_ns += gap_ns;
    m_display_gap_idx = (m_display_gap_idx + 1) % DISPLAY_GAP_SAMPLES;
    if (m_display_gap_count < DISPLAY_GAP_SAMPLES) { ++m_display_gap_count; }
}

void DirectorTransport::onDisconnected(livekit::Room & /*unused*/, const livekit::DisconnectedEvent &event) {
    livekit::DisconnectReason reason = event.reason;

    qDebug() << "[DirectorTransport] Disconnected from room."
             << "\n\treason=" << disconnectReasonToString(reason);

    QMetaObject::invokeMethod(this, [this]() {
        emit disconnected();
    }, Qt::QueuedConnection);
}

void DirectorTransport::connectToRoom(const QString &token, const QString &url) {
    if ((m_connectWatcher != nullptr) && m_connectWatcher->isRunning()) {
        qWarning() << "[DirectorTransport] Connection already in progress.";
        return;
    }

    if (token.isEmpty() || url.isEmpty()) {
        qWarning() << "[DirectorTransport] Empty room credentials given. Could not connect.";
        return;
    }

    livekit::RoomOptions opts;
    std::string std_url = url.toStdString();
    std::string std_token = token.toStdString();

    m_connection_state = "connecting";
    emit connectionStateChanged(m_connection_state);    

    qDebug() << "[DirectorTransport] Connecting to room."
             << "\n\ttoken=" << token
             << "\n\turl=" << url;
    
    if (!m_room) {
        m_room = std::make_unique<livekit::Room>();
        m_room->setDelegate(this);
    }


    // LiveKit's connect function blocks the thread it is in
    // Put it in a QFuture so the app does not freeze
    m_connectWatcher = new QFutureWatcher<bool>(this);

    connect(m_connectWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool success = m_connectWatcher->result();
        m_connectWatcher->deleteLater();
        m_connectWatcher = nullptr;

        if (m_connection_state == "disconnected") {
            qWarning() << "[DirectorTransport] Connection finished after disconnect; cancelling.";
            return;
        }

        if (success) {
            m_session = std::make_unique<DirectorSession>();
            connect(m_session.get(), &DirectorSession::frameArrived,
                    this, &DirectorTransport::onFrameArrived);
            connect(m_session.get(), &DirectorSession::videoStats,
                    this, &DirectorTransport::onVideoStats);
            connect(m_session.get(), &DirectorSession::videoResolutionChanged,
                    this, &DirectorTransport::videoResolutionChanged);
            qDebug() << "[DirectorTransport] Connected.";
            emit sessionChanged();
            emit connected();
        }
        else
        {
            qWarning() << "[DirectorTransport] Connection failed.";
        }
    });

    QFuture<bool> future = QtConcurrent::run([this, std_url, std_token, opts]() mutable {
        return m_room->Connect(std_url, std_token, opts);
    });
    
    m_connectWatcher->setFuture(future);
}

void DirectorTransport::disconnectFromRoom() {
    if (m_connection_state == "disconnected") { return; }
    shutdown();
    m_connection_state = "disconnected";
    qDebug() << "[DirectorTransport] Disconnected.";
    emit connectionStateChanged(m_connection_state);
    emit sessionChanged();
    emit disconnected();
}

void DirectorTransport::shutdown() {
    m_session.reset();
    m_room.reset();
    {
        std::lock_guard<std::mutex> lock(m_active_participant_mutex);
        m_active_participant_identity.clear();
    }
    resetLatencyMatcher();
}

void DirectorTransport::setActiveParticipant(const QString &identity) {
    {
        std::lock_guard<std::mutex> lock(m_active_participant_mutex);
        if (m_active_participant_identity == identity) {
            return;
        }
        m_active_participant_identity = identity;
    }
    qDebug() << "[DirectorTransport] Active participant set."
             << "\n\tidentity=" << identity;
    resetLatencyMatcher();
}

void DirectorTransport::resetLatencyMatcher() {
    {
        std::lock_guard<std::mutex> lock(m_capture_queue_mutex);
        m_capture_queue.clear();
    }
    m_ts_offset_initialized = false;
    m_ts_capture_offset_ns = 0;
    m_ts_consecutive_misses = 0;
    m_estimated_video_lag_ns = INITIAL_VIDEO_LAG_GUESS_NS;
    m_settling_samples_remaining = SETTLING_SAMPLES;
    // Blank the displayed breakdown immediately so the UI doesn't keep showing
    // a value attributed to the previous camera while the new camera's
    // matcher seeds.  Display gap stays meaningful (it's per-render, not
    // per-camera), so emit zero for it as well to match the blank-on-switch
    // contract — the next valid match will refill all three after settling.
    emit latencyBreakdown(0.0, 0.0, 0.0);
    // Same blank-on-switch contract for the per-track stats — the new
    // camera's first getStats() poll arrives within a second.
    emit videoStatsBreakdown(0.0, 0.0, 0.0, 0.0);
}

void DirectorTransport::setClockOffset(qint64 ns) {
    m_clock_offset_ns.store(ns, std::memory_order_relaxed);
}

void DirectorTransport::setWindow(QObject *window) {
    auto *qw = qobject_cast<QQuickWindow *>(window);
    if (m_window != nullptr) {
        disconnect(m_window, &QQuickWindow::frameSwapped, this, nullptr);
    }
    m_window = qw;
    if (m_window != nullptr) {
        // frameSwapped is emitted from the render thread.  Stamp the swap
        // time there with Qt::DirectConnection so the timestamp reflects the
        // actual swap moment, not whenever the main thread happens to dequeue
        // a queued slot (which under main-thread load can add tens of ms of
        // pure dispatch lag to display_gap).  The bookkeeping that updates
        // shared rolling-average state is then posted to the main thread,
        // keeping that state single-threaded.
        connect(m_window, &QQuickWindow::frameSwapped, this, [this]() {
            const qint64 swap_steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            QMetaObject::invokeMethod(this, [this, swap_steady_ns]() {
                onFrameSwapped(swap_steady_ns);
            }, Qt::QueuedConnection);
        }, Qt::DirectConnection);
    }
}

double DirectorTransport::displayGapMs() const {
    if (m_display_gap_count == 0) { return 0.0; }
    return static_cast<double>(m_display_gap_sum_ns) / m_display_gap_count / 1e6;
}

QString DirectorTransport::connectionState() const {
    return m_connection_state;
}

DirectorSession *DirectorTransport::session() const {
    return m_session.get();
}