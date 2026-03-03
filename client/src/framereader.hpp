#pragma once

#include <QObject>
#include <QQmlEngine>
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
    QML_ELEMENT
    QML_SINGLETON

public:
    FrameReader(QObject *parent = nullptr);

    static FrameReader *create(QQmlEngine *, QJSEngine *) {
        static FrameReader instance;
        return &instance;
    }

    [[nodiscard]] QVideoSink *videoSink() const;

    void setVideoSink(QVideoSink *sink);
    void pushFrame(std::unique_ptr<videoCore::Frame> frame);

signals:
    void videoSinkChanged();

private:
    QVideoSink *m_videoSink {nullptr};
};
