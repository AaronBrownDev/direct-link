#include "videotrack.hpp"

#include <QDebug>
#include <QPointer>
#include <livekit/stats.h>
#include <livekit/video_frame.h>

#include <chrono>
#include <variant>
#include <vector>

#include "../../video-core/include/common/latency_overlay.hpp"

VideoTrack::VideoTrack(QObject *parent) : QObject{parent} {
    m_frameReader = std::make_unique<FrameReader>();
    connect(m_frameReader.get(), &FrameReader::videoSinkChanged, this, &VideoTrack::onVideoSinkChanged);
    m_stats_timer = new QTimer(this);
    m_stats_timer->setInterval(STATS_POLL_INTERVAL_MS);
    connect(m_stats_timer, &QTimer::timeout, this, &VideoTrack::pollStats);
}

VideoTrack::~VideoTrack() {
    unsetTrack();
}

QVideoSink *VideoTrack::videoSink() const {
    return m_frameReader->videoSink();
}

qreal VideoTrack::aspectRatio() const {
    return m_aspectRatio.load();
}

QString VideoTrack::participantIdentity() const {
    return m_participant_identity;
}

void VideoTrack::setParticipantIdentity(const QString &identity) {
    m_participant_identity = identity;
}

void VideoTrack::setVideoSink(QVideoSink *sink) {
    // Same value cannot be set twice to prevent unnecessary emission
    if (m_frameReader->videoSink() != sink) {
        m_frameReader->setVideoSink(sink);
        emit videoSinkChanged();
    }
}

bool VideoTrack::setTrack(const std::shared_ptr<livekit::Track> &track) {
    unsetTrack();

    livekit::VideoStream::Options opts;
    opts.format = livekit::VideoBufferType::I420;

    m_stream = livekit::VideoStream::fromTrack(track, opts);
    if (!m_stream) {
        qDebug() << "[VideoTrack] Failed to create VideoStream.";
        return false;
    }

    // Hold the track for getStats() polling.  fromTrack() above only stores
    // the FFI handle, so without this the Track object would go out of scope
    // and stats polling would have no live receiver to query.
    m_track = track;
    // Bump generation again now that the new track is in place — any worker
    // dispatched after this sees the new value, while any in-flight worker
    // from the old track has already had its captured generation invalidated
    // by the unsetTrack() above.
    m_track_generation.fetch_add(1, std::memory_order_relaxed);

    startRead();
    m_stats_timer->start();

    return true;
}

void VideoTrack::unsetTrack() {
    if (m_stats_timer != nullptr) {
        m_stats_timer->stop();
    }
    // Invalidate any in-flight stats workers BEFORE tearing down state, so
    // their main-thread continuations bail at the generation check instead
    // of mutating m_prev_* / m_stats_initialized.
    m_track_generation.fetch_add(1, std::memory_order_relaxed);
    m_stats_initialized = false;
    // Wait for the in-flight stats worker (if any) to return from the FFI
    // getStats().get() blocking call before letting livekit::Room get torn
    // down by the caller — otherwise the worker can crash inside libwebrtc
    // mid-call.  The worker's main-thread continuation will still run after
    // this wait, but it bails at the generation check above.
    if (m_stats_future.isValid() && m_stats_future.isRunning()) {
        m_stats_future.waitForFinished();
    }
    if (m_stream) {
        m_stream->close();
    }
    if (m_readFuture.isRunning()) {
        m_readFuture.waitForFinished();
    }
    m_stream.reset();
    m_track.reset();
}

void VideoTrack::onVideoSinkChanged() {
    if (m_stream && (m_frameReader->videoSink() != nullptr) && !m_readFuture.isRunning()) {
        startRead();
    }
}

void VideoTrack::startRead() {
    if (m_readFuture.isRunning()) { return; }

    m_readFuture = QtConcurrent::run([this]() {
        readLoop();
    });
}

void VideoTrack::pollStats() {
    if (!m_track) { return; }

    // Skip this tick if the previous worker is still in flight.  Stats are
    // a low-priority diagnostic — losing one sample is fine, and it keeps
    // the QFuture single-slot so unsetTrack() only has to wait on one
    // outstanding worker.
    if (m_stats_future.isValid() && m_stats_future.isRunning()) {
        return;
    }

    // getStats() returns a std::future; .get() blocks waiting for the FFI
    // round-trip.  Run on a worker thread to avoid stalling the QML thread,
    // then post the parsed result back here.  Capture the track by shared_ptr
    // copy so the receiver stays alive even if unsetTrack() races with this.
    auto track = m_track;
    QPointer<VideoTrack> self(this);
    const QString identity = m_participant_identity;
    const std::uint64_t generation = m_track_generation.load(std::memory_order_relaxed);
    m_stats_future = QtConcurrent::run([self, track, identity, generation]() {
        std::vector<livekit::RtcStats> stats_vec;
        try {
            stats_vec = track->getStats().get();
        } catch (const std::exception &e) {
            qWarning() << "[VideoTrack] getStats failed:" << e.what();
            return;
        }

        // Find the inbound-RTP video stream stats.  A track typically has
        // exactly one inbound-RTP entry; pick the first one whose stream.kind
        // is "video" so we ignore any audio entries on a hypothetical
        // multi-track track in the future.
        const livekit::InboundRtpStreamStats *inbound = nullptr;
        const livekit::ReceivedRtpStreamStats *received = nullptr;
        for (const auto &rtc : stats_vec) {
            if (const auto *in = std::get_if<livekit::RtcInboundRtpStats>(&rtc.stats)) {
                if (in->stream.kind == "video") {
                    inbound = &in->inbound;
                    received = &in->received;
                    break;
                }
            }
        }
        if (inbound == nullptr) { return; }

        const double jitter_buffer_delay_s = inbound->jitter_buffer_delay;
        const std::uint64_t jitter_buffer_emitted = inbound->jitter_buffer_emitted_count;
        const double total_decode_time_s = inbound->total_decode_time;
        const std::uint32_t frames_decoded = inbound->frames_decoded;
        const double network_jitter_s = (received != nullptr) ? received->jitter : 0.0;
        const double frames_per_second = inbound->frames_per_second;

        QMetaObject::invokeMethod(self.data(), [self, identity, generation,
                                                jitter_buffer_delay_s,
                                                jitter_buffer_emitted,
                                                total_decode_time_s,
                                                frames_decoded,
                                                network_jitter_s,
                                                frames_per_second]() {
            if (!self) { return; }
            // Drop stale results from a track that was already replaced.
            // Without this, this lambda would mutate the m_prev_* state of
            // the *new* track using the old track's stats values.
            if (self->m_track_generation.load(std::memory_order_relaxed) != generation) {
                return;
            }

            // First sample only seeds the baseline — no delta to emit yet.
            if (!self->m_stats_initialized) {
                self->m_prev_jitter_buffer_delay_s = jitter_buffer_delay_s;
                self->m_prev_jitter_buffer_emitted = jitter_buffer_emitted;
                self->m_prev_total_decode_time_s = total_decode_time_s;
                self->m_prev_frames_decoded = frames_decoded;
                self->m_stats_initialized = true;
                return;
            }

            const double djbd_s = jitter_buffer_delay_s - self->m_prev_jitter_buffer_delay_s;
            const std::uint64_t djbe = jitter_buffer_emitted - self->m_prev_jitter_buffer_emitted;
            const double dtdt_s = total_decode_time_s - self->m_prev_total_decode_time_s;
            const std::uint32_t dfd = frames_decoded - self->m_prev_frames_decoded;

            self->m_prev_jitter_buffer_delay_s = jitter_buffer_delay_s;
            self->m_prev_jitter_buffer_emitted = jitter_buffer_emitted;
            self->m_prev_total_decode_time_s = total_decode_time_s;
            self->m_prev_frames_decoded = frames_decoded;

            // Per-frame means.  Emit zero rather than NaN when the
            // denominator is zero — the polling interval can fall in a
            // gap with no decoded frames during a freeze.
            const double jitter_buffer_ms = (djbe > 0) ? (djbd_s / static_cast<double>(djbe)) * 1000.0 : 0.0;
            const double decode_ms = (dfd > 0) ? (dtdt_s / dfd) * 1000.0 : 0.0;
            const double network_jitter_ms = network_jitter_s * 1000.0;

            emit self->videoStats(jitter_buffer_ms, decode_ms,
                                  network_jitter_ms, frames_per_second,
                                  identity);
        }, Qt::QueuedConnection);
    });
}

void VideoTrack::readLoop() {
    livekit::VideoFrameEvent event;
    bool has_ratio = false;

    while (m_stream && m_stream->read(event)) {
        // Record receive time immediately after the blocking read returns —
        // this is when the decoded frame is first available to the application.
        // steady_clock so director-local intervals (video_lag = received -
        // dc_arrived; display_gap = swap - received) survive an NTP step on
        // the wall clock.
        const qint64 received_steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (!has_ratio) {
            int w = event.frame.width();
            int h = event.frame.height();
            if (h > 0) {
                m_aspectRatio.store(static_cast<qreal>(w) / h);
                QMetaObject::invokeMethod(this, &VideoTrack::aspectRatioChanged, Qt::QueuedConnection);
                QMetaObject::invokeMethod(this, [this, w, h]() {
                    emit trackResolutionChanged(w, h);
                }, Qt::QueuedConnection);
                has_ratio = true;
            }
        }

        const qint64 frame_ts_us = event.timestamp_us;

        // Benchmark-mode overlay decode.  Sample the top-left 128x128
        // black/white pattern the camera drew into the Y plane (when run
        // with --benchmark-latency) and recover the embedded
        // server-domain capture timestamp.  Must happen before the
        // std::move below — pushFrame consumes event.frame.  The decode
        // gracefully no-ops on frames that don't carry the overlay (it
        // requires 90% of cells to be clearly black/white before
        // accepting), so normal frames pass through untouched.
        if (videoCore::benchmark::isLatencyOverlayEnabled() &&
            event.frame.type() == livekit::VideoBufferType::I420) {
            const auto planes = event.frame.planeInfos();
            if (!planes.empty() && planes[0].data_ptr != 0) {
                const auto *y_data = reinterpret_cast<const std::uint8_t *>(planes[0].data_ptr);
                std::uint64_t decoded = 0;
                if (videoCore::decodeTimestampOverlay(
                        y_data, static_cast<int>(planes[0].stride),
                        event.frame.width(), event.frame.height(), &decoded)) {
                    const qint64 received_wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    const auto decoded_signed = static_cast<qint64>(decoded);
                    QMetaObject::invokeMethod(this, [this, decoded_signed, received_wall_ns]() {
                        emit benchmarkOverlayDecoded(decoded_signed, received_wall_ns);
                    }, Qt::QueuedConnection);
                }
            }
        }

        if (m_frameReader->videoSink() != nullptr) {
            m_frameReader->pushFrame(std::move(event.frame));
        }

        // Capture identity by value into the lambda so the signal payload is
        // independent of any later mutation on the VideoTrack instance.
        const QString identity = m_participant_identity;
        QMetaObject::invokeMethod(this, [this, received_steady_ns, frame_ts_us, identity]() {
            emit frameReceived(received_steady_ns, frame_ts_us, identity);
        }, Qt::QueuedConnection);
    }
}