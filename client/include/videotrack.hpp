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

    public:
        explicit VideoTrack(QObject *parent = nullptr);
        ~VideoTrack() override;

        VideoTrack(const VideoTrack &) = delete;
        VideoTrack &operator=(const VideoTrack &) = delete;
        VideoTrack(VideoTrack &&) = delete;
        VideoTrack &operator=(VideoTrack &&) = delete;

        [[nodiscard]] QVideoSink *videoSink() const;
        [[nodiscard]] qreal aspectRatio() const;
        
        void setVideoSink(QVideoSink *sink);

        bool setTrack(const std::shared_ptr<livekit::Track> &track);
        void unsetTrack();

    signals:
        void videoSinkChanged();
        void aspectRatioChanged();

    private:
        std::unique_ptr<FrameReader> m_frameReader;
        std::shared_ptr<livekit::VideoStream> m_stream;
        QFuture<void> m_readFuture;
        std::atomic<qreal> m_aspectRatio{16.0 / 9.0};

        void startRead();
        void readLoop();

        Q_SLOT void onVideoSinkChanged();
};
