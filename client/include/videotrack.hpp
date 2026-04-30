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
#include <QVideoSink>
#include <livekit/video_stream.h>
#include <atomic>

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
        //   receivedNs            — local wall-clock ns captured immediately
        //                           after VideoStream::read() returns
        //                           (director clock).
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
        void frameReceived(qint64 receivedNs, qint64 frameTimestampUs,
                           const QString &participantIdentity);

    private:
        std::unique_ptr<FrameReader> m_frameReader;
        std::shared_ptr<livekit::VideoStream> m_stream;
        QFuture<void> m_readFuture;
        std::atomic<qreal> m_aspectRatio{16.0 / 9.0};
        QString m_participant_identity;

        void startRead();
        void readLoop();

        Q_SLOT void onVideoSinkChanged();
};
