#include "videotrack.hpp"

#include <chrono>

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

QString VideoTrack::participantIdentity() const {
    return m_participant_identity;
}

void VideoTrack::setParticipantIdentity(const QString &identity) {
    m_participant_identity = identity;
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
    opts.format = livekit::VideoBufferType::I420;

    m_stream = livekit::VideoStream::fromTrack(track, opts);
    if (!m_stream) {
        qDebug() << "[VideoTrack] Failed to create VideoStream.";
        return false;
    }

    startRead();

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
        // Record receive time immediately after the blocking read returns —
        // this is when the decoded frame is first available to the application.
        const qint64 received_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (!has_ratio) {
            int w = event.frame.width();
            int h = event.frame.height();
            if (h > 0) {
                m_aspectRatio.store(static_cast<qreal>(w) / h);
                QMetaObject::invokeMethod(this, &VideoTrack::aspectRatioChanged, Qt::QueuedConnection);
                QMetaObject::invokeMethod(this, [this, w, h]() {
                    emit trackResolutionChanged(w, h);
                }, Qt::QueuedConnection);
                has_ratio = true;
            }
        }

        const qint64 frame_ts_us = event.timestamp_us;

        if (m_frameReader->videoSink() != nullptr) {
            m_frameReader->pushFrame(std::move(event.frame));
        }

        // Capture identity by value into the lambda so the signal payload is
        // independent of any later mutation on the VideoTrack instance.
        const QString identity = m_participant_identity;
        QMetaObject::invokeMethod(this, [this, received_ns, frame_ts_us, identity]() {
            emit frameReceived(received_ns, frame_ts_us, identity);
        }, Qt::QueuedConnection);
    }
}