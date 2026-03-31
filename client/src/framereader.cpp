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

    // I420 is the native H.264 decoder output. Deliver directly to Qt as
    // YUV420P via planeInfos() for correct plane pointers/strides.
    if (frame.type() == livekit::VideoBufferType::I420) {
        if (m_videoSink == nullptr) {
            qWarning() << "[FrameReader] Video sink not set. Skipping frame.";
            return;
        }

        const int w = frame.width();
        const int h = frame.height();

        auto planes = frame.planeInfos();
        if (planes.size() < 3) {
            qWarning() << "[FrameReader] I420 frame has" << planes.size()
                       << "planes, expected 3. Falling back to SDK convert.";
            // Fall through to the SDK RGBA conversion path below.
        } else {
            const auto *y_plane = reinterpret_cast<const uint8_t *>(planes[0].data_ptr);
            const auto *u_plane = reinterpret_cast<const uint8_t *>(planes[1].data_ptr);
            const auto *v_plane = reinterpret_cast<const uint8_t *>(planes[2].data_ptr);
            const int y_stride = static_cast<int>(planes[0].stride);
            const int u_stride = static_cast<int>(planes[1].stride);
            const int v_stride = static_cast<int>(planes[2].stride);

            QVideoFrameFormat fmt(QSize(w, h), QVideoFrameFormat::Format_YUV420P);
            QVideoFrame vf(fmt);

            if (!vf.map(QVideoFrame::WriteOnly)) {
                qWarning() << "[FrameReader] Failed to map YUV420P frame.";
                return;
            }

            // Copy Y plane
            const int dst_y_stride = vf.bytesPerLine(0);
            auto *dst_y = vf.bits(0);
            for (int row = 0; row < h; ++row) {
                std::memcpy(dst_y + row * dst_y_stride,
                            y_plane + row * y_stride,
                            static_cast<size_t>(w));
            }

            // Copy U plane
            const int dst_u_stride = vf.bytesPerLine(1);
            auto *dst_u = vf.bits(1);
            for (int row = 0; row < h / 2; ++row) {
                std::memcpy(dst_u + row * dst_u_stride,
                            u_plane + row * u_stride,
                            static_cast<size_t>(w / 2));
            }

            // Copy V plane
            const int dst_v_stride = vf.bytesPerLine(2);
            auto *dst_v = vf.bits(2);
            for (int row = 0; row < h / 2; ++row) {
                std::memcpy(dst_v + row * dst_v_stride,
                            v_plane + row * v_stride,
                            static_cast<size_t>(w / 2));
            }

            vf.unmap();
            m_videoSink->setVideoFrame(vf);
            return;
        }
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
