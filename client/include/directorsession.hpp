#pragma once

#include <memory>

#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>
#include <QFuture>
#include <QtConcurrent>

#include "livekit/livekit.h"
#include "livekit/video_stream.h"
#include "framereader.hpp"
#include "decode/decoder.hpp"
#include "common/types.hpp"

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

    private:
        std::unique_ptr<videoCore::decode::Decoder> m_decoder;
        std::unique_ptr<FrameReader> m_framereader;
        std::shared_ptr<livekit::VideoStream> m_stream;
        QFuture<void> m_readfuture;

        void readLoop();
};
