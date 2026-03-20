/*
 * File: directorsession.hpp
 * Author: Justin Williams
 * Date: 3/17/26
 * File Description: A class that owns a FrameReader and accepts LiveKit tracks from
 * DirectorTransport. The class will establish a read loop and feed LiveKit video
 * frames into the frame reader while a track is attached. It manages the FrameReader's
 * videoSink object.
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVideoSink>
#include <QtConcurrent/QtConcurrent>

#include "livekit/livekit.h"
#include "framereader.hpp"

class DirectorSession : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

    public:
        explicit DirectorSession(QObject *parent = nullptr);
        ~DirectorSession() override;

        [[nodiscard]] QVideoSink *videoSink() const;

        void attachTrack(std::shared_ptr<livekit::Track> track);
        void detachTrack();

        void setVideoSink(QVideoSink *sink);

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
