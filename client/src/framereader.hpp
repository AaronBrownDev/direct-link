#pragma once

#include <QObject>
#include <QImage>
#include <QSize>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QDebug>
#include <iostream>
#include "../../video-core/include/common/types.hpp"

class FrameReader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

public:
    FrameReader(QObject *parent = nullptr);

    [[nodiscard]] QVideoSink *videoSink() const;

    void setVideoSink(QVideoSink *sink);
    void pushFrame(std::unique_ptr<videoCore::Frame> frame);

signals:
    void videoSinkChanged();

private:
    QVideoSink *m_videoSink;
};
