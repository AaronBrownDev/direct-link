#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QImage>
#include <QSize>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QDebug>
#include <span>

#include "livekit/livekit.h"

class FrameReader : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

public:
    FrameReader(QObject *parent = nullptr);

    [[nodiscard]] QVideoSink *videoSink() const;

    void setVideoSink(QVideoSink *sink);
    void pushFrame();
    void pushFrame(livekit::VideoFrame frame);

signals:
    void videoSinkChanged();

private:
    QVideoSink *m_videoSink {nullptr};
    QImage m_placeholder;

    bool pushSpan(std::span<const uint8_t> data, QSize dimensions, QVideoFrameFormat::PixelFormat format);
};
