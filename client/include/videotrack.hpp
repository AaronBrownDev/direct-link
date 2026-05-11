/*
 * File: videotrack.hpp
 * Author: Justin Williams
 * Date: 3/26/26
 * File Description: A class that owns a FrameReader and accepts LiveKit tracks from
 * DirectorSession. Video streams are created from the livekit tracks when a video sink 
 * is attached to the frame reader. Frames are read from the video stream in a read loop
 * and pushed into the FrameReader object to display them in the app.
 */


#pragma once

#include <QObject>
#include <QtConcurrent/QtConcurrent>
#include <QTimer>
#include <QVideoSink>
#include <livekit/track.h>
#include <livekit/video_stream.h>
#include <atomic>
#include <cstdint>

#include "framereader.hpp"

class VideoTrack : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("VideoTrack is managed by DirectorSession.")

    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(qreal aspectRatio READ aspectRatio NOTIFY aspectRatioChanged)
    Q_PROPERTY(QString participantIdentity READ participantIdentity CONSTANT)

    public:
        explicit VideoTrack(QObject *parent = nullptr);
        ~VideoTrack() override;

        VideoTrack(const VideoTrack &) = delete;
        VideoTrack &operator=(const VideoTrack &) = delete;
        VideoTrack(VideoTrack &&) = delete;
        VideoTrack &operator=(VideoTrack &&) = delete;

        [[nodiscard]] QVideoSink *videoSink() const;
        [[nodiscard]] qreal aspectRatio() const;
        // Identity of the remote participant publishing this track.  Set once
        // by DirectorSession when the track is attached and not mutated after,
        // so the property is CONSTANT for QML binding purposes.
        [[nodiscard]] QString participantIdentity() const;

        void setVideoSink(QVideoSink *sink);
        // Must be called before setTrack() if the identity should be carried
        // through frameReceived for downstream filtering (latency matcher).
        void setParticipantIdentity(const QString &identity);

        bool setTrack(const std::shared_ptr<livekit::Track> &track);
        void unsetTrack();

    signals:
        void videoSinkChanged();
        void aspectRatioChanged();
        // Emitted once (via QueuedConnection) the first time a frame with valid
        // dimensions is decoded. width and height are in pixels.
        void trackResolutionChanged(int width, int height);
        // Emitted (via QueuedConnection) on the main thread each time a decoded
        // frame arrives in the read loop.
        //   receivedSteadyNs      — steady_clock ns captured immediately after
        //                           VideoStream::read() returns.  All
        //                           director-local intervals (video_lag,
        //                           display_gap) use this so an NTP step can't
        //                           corrupt the measurement.
        //   frameTimestampUs      — VideoFrameEvent::timestamp_us from
        //                           livekit-cpp; libwebrtc-aligned
        //                           capture-time estimate (sender domain,
        //                           microseconds). Used by DirectorTransport
        //                           to match frames to DC packets by
        //                           timestamp.
        //   participantIdentity   — identity of the publishing participant.
        //                           Allows DirectorTransport to filter frames
        //                           down to the active camera so the matcher
        //                           sees a single sender's clock domain.
        void frameReceived(qint64 receivedSteadyNs, qint64 frameTimestampUs,
                           const QString &participantIdentity);
        // Periodic breakdown of where video_lag is being spent on the
        // director side, sampled from libwebrtc's getStats() and reported
        // as a per-frame mean over the last polling interval.  participantIdentity
        // lets DirectorTransport filter to the active main-preview camera.
        //   jitterBufferMs — mean ms each emitted frame waited in the
        //                    director-side jitter buffer
        //                    (jitter_buffer_delay / jitter_buffer_emitted_count)
        //   decodeMs       — mean H.264 decode time per frame
        //                    (total_decode_time / frames_decoded)
        //   networkJitterMs — RFC inter-arrival jitter on the inbound RTP
        //                     stream (jitter * 1000)
        //   framesPerSecond — frames_per_second from the inbound stats
        //                     (mostly a sanity signal: if this drops while
        //                     video_lag holds, the matcher is showing a
        //                     stale frame age)
        void videoStats(double jitterBufferMs, double decodeMs,
                        double networkJitterMs, double framesPerSecond,
                        const QString &participantIdentity);
        // Benchmark-mode ground-truth latency.  Emitted per frame when the
        // camera-side latency overlay was successfully decoded from the Y
        // plane.  decodedServerNs is the camera-side server-domain
        // timestamp the camera embedded into the captured pixels;
        // receivedWallNs is the director's local wall clock at frame
        // read-out.  DirectorTransport pairs them with its own clock
        // offset to compute capture-to-receive latency without any matcher
        // involvement — the gold-standard measurement.
        void benchmarkOverlayDecoded(qint64 decodedServerNs, qint64 receivedWallNs);

    private:
        std::unique_ptr<FrameReader> m_frameReader;
        std::shared_ptr<livekit::Track> m_track;
        std::shared_ptr<livekit::VideoStream> m_stream;
        QFuture<void> m_readFuture;
        std::atomic<qreal> m_aspectRatio{16.0 / 9.0};
        QString m_participant_identity;

        // Periodic getStats() polling: every STATS_POLL_INTERVAL_MS we ask
        // libwebrtc for its inbound-RTP stats and emit per-frame means as
        // deltas against the previous sample.  All stats fields exposed by
        // the SDK are cumulative since stream start, so a delta over a
        // known interval gives the per-second / per-frame view that's
        // actually useful for attribution.
        //
        // The future tracks the in-flight worker so unsetTrack() can wait
        // for it before tearing down the receiver.  Without this wait, an
        // FFI getStats() call can be in flight while livekit::Room is
        // destroyed at process exit — segfault inside libwebrtc.  At most
        // one worker exists at a time: pollStats() skips a tick if the
        // previous worker hasn't returned yet.
        QTimer *m_stats_timer = nullptr;
        QFuture<void> m_stats_future;
        static constexpr int STATS_POLL_INTERVAL_MS = 1000;

        // Previous-sample state for delta computation.  jbd/tdt/tpd are in
        // SECONDS (libwebrtc convention) and accumulate across the lifetime
        // of the receiver; jbe/fd are unitless counters.  Initialised on the
        // first successful getStats() result; until then m_stats_initialized
        // is false and we just record the baseline without emitting.
        bool m_stats_initialized = false;
        double m_prev_jitter_buffer_delay_s = 0.0;
        std::uint64_t m_prev_jitter_buffer_emitted = 0;
        double m_prev_total_decode_time_s = 0.0;
        std::uint32_t m_prev_frames_decoded = 0;

        // Each set/unsetTrack() bumps this counter.  Workers and main-thread
        // continuations capture its value at dispatch time and bail if it
        // doesn't match the current value when they run.  Without this, an
        // in-flight stats worker from track A can race with setTrack(B) and
        // either seed the new track's baseline with A's values (producing a
        // garbage first delta) or overwrite a freshly-seeded baseline.
        std::atomic<std::uint64_t> m_track_generation{0};

        void startRead();
        void readLoop();
        void pollStats();

        Q_SLOT void onVideoSinkChanged();
};
