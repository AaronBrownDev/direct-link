/*
 * File: framereader.cpp
 * Author: Justin Williams
 * Date: 2/21/26
 * File Description: A class implementing a frame reader. The frame reader accepts
 * AVFrame objects and wraps them in a QVideoFrame. A QVideoSink accepts
 * a QVideoFrame and updates a VideoOutput QML object to display the frame in the app.
 */

#include "framereader.hpp"



FrameReader::FrameReader(QObject *parent) : QObject(parent) {

}

QVideoSink *FrameReader::videoSink() const {
    return m_videoSink;
}

void FrameReader::setVideoSink(QVideoSink *sink) {
    // Same value cannot be set twice to prevent unnecessary emission
    if (m_videoSink != sink) {
        m_videoSink = sink;
        pushFrame(nullptr);
        emit videoSinkChanged();
    }
}

void FrameReader::pushFrame(std::unique_ptr<videoCore::Frame> frame) {
    if (m_videoSink == nullptr) {
        qDebug() << "Could not push frame. Video sink has not been set.";
        return;
    }

    QImage placeholder(":/resources/ui/placeholderSplash.png");
    placeholder = placeholder.convertToFormat(QImage::Format_RGBX8888);
    const uchar *src = nullptr;
    std::size_t size = 0;

    if (frame != nullptr) {
        qDebug() << "Frame support not yet implemented. Defaulting to placeholder frame.";
    }

    QSize frame_dimensions(placeholder.width(), placeholder.height());
    // TODO: Change to span
    src = placeholder.bits();
    size = placeholder.sizeInBytes();

    QVideoFrameFormat format(
        frame_dimensions,
        QVideoFrameFormat::Format_RGBX8888);
    QVideoFrame video_frame(format);

    video_frame.map(QVideoFrame::WriteOnly);
    std::memcpy(video_frame.bits(0), src, size);
    video_frame.unmap();

    m_videoSink->setVideoFrame(video_frame);
}
