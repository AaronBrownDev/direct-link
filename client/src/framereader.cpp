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

    if (src == nullptr || size == 0) {
        qWarning() << "[FrameReader] Received empty frame. Pushing placeholder frame.";
        pushFrame();
        return;
    }

    QSize dimensions(frame.width(), frame.height());

    if (dimensions.isEmpty()) {
        qWarning() << "[FrameReader] Frame has no dimensions. Pushing placeholder frame.";
        pushFrame();
        return;
    }

    qDebug() << "[FrameReader] incoming:"
             << "type=" << static_cast<int>(frame.type())
             << "dataSize=" << static_cast<int>(frame.dataSize())
             << "dims=" << frame.width() << "x" << frame.height();

    // I420 is the native H.264 decoder output. Push it directly as YUV420P so
    // Qt's VideoOutput can use its native YUV shader, bypassing the SDK-side
    // I420→RGBA conversion that was producing the 4-quadrant color artifact.
    if (frame.type() == livekit::VideoBufferType::I420) {
        if (m_videoSink == nullptr) {
            qWarning() << "[FrameReader] Video sink not set. Skipping frame.";
            return;
        }

        const int w      = frame.width();
        const int h      = frame.height();
        const int y_size = w * h;
        const int uv_size = (w / 2) * (h / 2);

        QVideoFrameFormat fmt(dimensions, QVideoFrameFormat::Format_YUV420P);
        QVideoFrame vf(fmt);

        if (!vf.map(QVideoFrame::WriteOnly)) {
            qWarning() << "[FrameReader] Failed to map YUV420P frame. Pushing placeholder.";
            pushFrame();
            return;
        }

        // Copy each plane row-by-row to respect Qt's internal stride padding.
        const uint8_t *y_src = src;
        const uint8_t *u_src = src + y_size;
        const uint8_t *v_src = src + y_size + uv_size;

        for (int row = 0; row < h; ++row) {
            const auto dst_y  = static_cast<ptrdiff_t>(row) * vf.bytesPerLine(0);
            const auto src_y  = static_cast<ptrdiff_t>(row) * w;
            std::memcpy(vf.bits(0) + dst_y, y_src + src_y, static_cast<std::size_t>(w));
        }
        for (int row = 0; row < h / 2; ++row) {
            const auto dst_u  = static_cast<ptrdiff_t>(row) * vf.bytesPerLine(1);
            const auto dst_v  = static_cast<ptrdiff_t>(row) * vf.bytesPerLine(2);
            const auto src_uv = static_cast<ptrdiff_t>(row) * (w / 2);
            std::memcpy(vf.bits(1) + dst_u, u_src + src_uv, static_cast<std::size_t>(w / 2));
            std::memcpy(vf.bits(2) + dst_v, v_src + src_uv, static_cast<std::size_t>(w / 2));
        }

        vf.unmap();
        m_videoSink->setVideoFrame(vf);
        return;
    }

    // Fallback: convert to RGBA and push as RGBA8888.
    if (frame.type() != livekit::VideoBufferType::RGBA) {
        qWarning() << "[FrameReader] Unexpected frame type"
                   << static_cast<int>(frame.type()) << ". Converting to RGBA.";
        try {
            frame = frame.convert(livekit::VideoBufferType::RGBA);
        } catch (const std::exception &e) {
            qWarning() << "[FrameReader] Frame conversion to RGBA failed:" << e.what()
                       << ". Pushing placeholder frame.";
            pushFrame();
            return;
        }
    }

    std::span<const uint8_t> data(frame.data(), frame.dataSize());
    bool success = pushSpan(data, dimensions, QVideoFrameFormat::Format_RGBA8888);

    if (!success) {
        qWarning() << "[FrameReader] Failed to push frame. Pushing placeholder frame.";
        pushFrame();
    }
}
