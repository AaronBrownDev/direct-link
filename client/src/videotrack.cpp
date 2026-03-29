#include "videotrack.hpp"

VideoTrack::VideoTrack(QObject *parent) : QObject{parent} {
    m_frameReader = std::make_unique<FrameReader>();
    connect(m_frameReader.get(), &FrameReader::videoSinkChanged, this, &VideoTrack::onVideoSinkChanged);
}

VideoTrack::~VideoTrack() {
    unsetTrack();
}

QVideoSink *VideoTrack::videoSink() const {
    return m_frameReader->videoSink();
}

qreal VideoTrack::aspectRatio() const {
    return m_aspectRatio.load();
}

void VideoTrack::setVideoSink(QVideoSink *sink) {
    // Same value cannot be set twice to prevent unnecessary emission
    if (m_frameReader->videoSink() != sink) {
        m_frameReader->setVideoSink(sink);
        emit videoSinkChanged();
    }
}

bool VideoTrack::setTrack(const std::shared_ptr<livekit::Track> &track) {
    unsetTrack();

    livekit::VideoStream::Options opts;
    // Request I420 (native H.264 decoder output) instead of RGBA.
    // Pushing I420 directly as QVideoFrameFormat::Format_YUV420P lets Qt's
    // VideoOutput use its native YUV shader path, which is more reliable than
    // the SDK-side I420→RGBA conversion that was producing color artifacts.
    opts.format = livekit::VideoBufferType::I420;

    m_stream = livekit::VideoStream::fromTrack(track, opts);
    if (!m_stream) {
        qDebug() << "[VideoTrack] Failed to create VideoStream.";
        return false;
    }

    if (m_frameReader->videoSink() != nullptr) {
        startRead();
    }

    return true;
}

void VideoTrack::unsetTrack() {
    if (m_stream) {
        m_stream->close();
    }
    if (m_readFuture.isRunning()) {
        m_readFuture.waitForFinished();
    }
    m_stream.reset();
}

void VideoTrack::onVideoSinkChanged() {
    if (m_stream && (m_frameReader->videoSink() != nullptr) && !m_readFuture.isRunning()) {
        startRead();
    }
}

void VideoTrack::startRead() {
    if (m_readFuture.isRunning()) { return; }

    m_readFuture = QtConcurrent::run([this]() {
        readLoop();
    });
}

void VideoTrack::readLoop() {
    livekit::VideoFrameEvent event;
    bool has_ratio = false;
    
    while (m_stream && m_stream->read(event)) {
        if (!has_ratio) {
            int w = event.frame.width();
            int h = event.frame.height();
            if (h > 0) {
                m_aspectRatio.store(static_cast<qreal>(w) / h);
                QMetaObject::invokeMethod(this, &VideoTrack::aspectRatioChanged, Qt::QueuedConnection);
                has_ratio = true;
            }
        }

       m_frameReader->pushFrame(std::move(event.frame));
    }
}