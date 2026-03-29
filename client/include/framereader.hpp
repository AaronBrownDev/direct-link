/*
 * File: framereader.hpp
 * Author: Justin Williams
 * Date: 2/21/26
 * File Description: A class implementing a frame reader. The frame reader accepts
 * LiveKit::VideoFrame objects and moves the data into a QVideoFrame. A QVideoSink accepts
 * a QVideoFrame and updates a connected VideoOutput QML object to display the frame in the app.
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QImage>
#include <QSize>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QDebug>
#include <livekit/video_frame.h>
#include <span>

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
