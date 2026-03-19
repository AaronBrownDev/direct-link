#include "directorsession.hpp"

DirectorSession::DirectorSession(QObject *parent) : QObject{parent} {
    m_frameReader = std::make_unique<FrameReader>();
    connect(m_frameReader.get(), &FrameReader::videoSinkChanged, this, &DirectorSession::onVideoSinkChanged);
}

DirectorSession::~DirectorSession() {
    detachTrack();
}

QVideoSink *DirectorSession::videoSink() const {
    return m_frameReader->videoSink();
}

void DirectorSession::setVideoSink(QVideoSink *sink) {
    // Same value cannot be set twice to prevent unnecessary emission
    if (m_frameReader->videoSink() != sink) {
        m_frameReader->setVideoSink(sink);
        emit videoSinkChanged();
    }
}

void DirectorSession::attachTrack(std::shared_ptr<livekit::Track> track) {
    detachTrack();

    livekit::VideoStream::Options opts;
    opts.format = livekit::VideoBufferType::RGBA;

    m_stream = livekit::VideoStream::fromTrack(track, opts);
    if (!m_stream) {
        qDebug() << "[DirectorSession] Failed to create VideoStream.";
        return;
    }

    if (m_frameReader->videoSink()) {
        startRead();
    }
    
}

void DirectorSession::detachTrack() {
    if (m_stream) {
        m_stream->close();
    }
    if (m_readFuture.isRunning()) {
        m_readFuture.waitForFinished();
    }
    m_stream.reset();
}

void DirectorSession::onVideoSinkChanged() {
    if (m_stream && m_frameReader->videoSink() && !m_readFuture.isRunning()) {
        startRead();
    }
}

void DirectorSession::startRead() {
    if (m_readFuture.isRunning()) { return; }

    m_readFuture = QtConcurrent::run([this]() {
        readLoop();
    });
}

void DirectorSession::readLoop() {
    livekit::VideoFrameEvent event;
    
    while (m_stream && m_stream->read(event)) {
       m_frameReader->pushFrame(std::move(event.frame));
    }
}