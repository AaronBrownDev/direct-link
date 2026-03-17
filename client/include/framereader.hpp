#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>
#include <QImage>
#include <QSize>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QDebug>
#include <iostream>
#include "common/types.hpp"

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
    // void pushFrame(std::unique_ptr<videoCore::Frame> frame);
    void pushFrame(livekit::VideoFrame frame);

signals:
    void videoSinkChanged();

private:
    QVideoSink *m_videoSink {nullptr};
};
