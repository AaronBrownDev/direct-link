#include "framereader.hpp"

FrameReader::FrameReader(QObject *parent) : QObject(parent) {
    m_placeholder = QImage(":/resources/ui/placeholderSplash.png").convertToFormat(QImage::Format_RGBX8888);
}

QVideoSink *FrameReader::videoSink() const {
    return m_videoSink;
}

void FrameReader::setVideoSink(QVideoSink *sink) {
    // Same value cannot be set twice to prevent unnecessary emission
    if (m_videoSink != sink) {
        m_videoSink = sink;
        pushFrame();
        emit videoSinkChanged();
    }
}

bool FrameReader::pushSpan(std::span<const uint8_t> data, QSize dimensions, QVideoFrameFormat::PixelFormat format) {
    if (m_videoSink == nullptr) {
        qWarning() << "[FrameReader] Failed to push frame. Video sink has not been set.";
        return false;
    }

    QVideoFrameFormat video_format(dimensions, format);
    QVideoFrame video_frame(video_format);

    if (!video_frame.map(QVideoFrame::WriteOnly)) {
        qWarning() << "[FrameReader] Failed to map to video frame.";
        return false;
    }

    std::memcpy(video_frame.bits(0), data.data(), data.size_bytes());
    video_frame.unmap();

    m_videoSink->setVideoFrame(video_frame);
    return true;
}

void FrameReader::pushFrame() {
    if (m_placeholder.isNull()) {
        qWarning() << "[FrameReader] Placeholder image failed to load.";
        return;
    }

    std::span<const uint8_t> data(m_placeholder.bits(), m_placeholder.sizeInBytes());
    QSize frame_dimensions(m_placeholder.width(), m_placeholder.height());

    pushSpan(data, frame_dimensions, QVideoFrameFormat::Format_RGBX8888);
}

void FrameReader::pushFrame(livekit::VideoFrame frame) {
    const uint8_t *src = frame.data();
    const std::size_t size = frame.dataSize();

    if ((src == nullptr) || size == 0) {
        qWarning() << "[FrameReader] Received empty frame. Pushing placeholder frame.";
        pushFrame();
        return;
    }

    QSize dimensions(static_cast<int>(frame.width()), static_cast<int>(frame.height()));

    if (dimensions.isEmpty()) {
        qWarning() << "[FrameReader] Frame has no dimensions. Pushing placeholder frame.";
        pushFrame();
        return;
    }
    
    std::span<const uint8_t> data(src, size);
    bool success = pushSpan(data, dimensions, QVideoFrameFormat::Format_RGBA8888);

    if (!success) {
        qWarning() << "[FrameReader] Failed to push frame. Pushing placeholder frame.";
        pushFrame();
    }
}
