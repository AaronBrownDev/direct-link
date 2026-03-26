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

#include "livekit/livekit.h"
#include "framereader.hpp"

class VideoTrack : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

    public:
        explicit VideoTrack(QObject *parent = nullptr);
        ~VideoTrack() override;

        [[nodiscard]] QVideoSink *videoSink() const;
        
        void setVideoSink(QVideoSink *sink);

        void setTrack(const std::shared_ptr<livekit::Track> &track);
        void unsetTrack();

    signals:
        void videoSinkChanged();

    private slots:
        void onVideoSinkChanged();

    private:
        std::unique_ptr<FrameReader> m_frameReader;
        std::shared_ptr<livekit::VideoStream> m_stream;
        QFuture<void> m_readFuture;

        void startRead();
        void readLoop();
};
