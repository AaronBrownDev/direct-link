#include "directorsession.hpp"

DirectorSession::DirectorSession(QObject *parent) : QObject{parent} {
    m_decoder = videoCore::decode::createDecoder();
    m_framereader = std::make_unique<FrameReader>();
}

DirectorSession::~DirectorSession() {
    detachTrack();
}

QVideoSink *DirectorSession::videoSink() const {
    return m_framereader->videoSink();
}

void DirectorSession::setVideoSink(QVideoSink *sink) {
    // Same value cannot be set twice to prevent unnecessary emission
    if (m_framereader->videoSink() != sink) {
        m_framereader->setVideoSink(sink);
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

    // TODO: re-add decoder after fetching raw data

    // auto result = m_decoder->initialize([this](std::unique_ptr<videoCore::Frame> frame) {
    //     if (m_framereader->videoSink()) {
    //         m_framereader->pushFrame(std::move(frame));
    //     }
    // });

    // if (result != videoCore::Result::Success) {
    //     qDebug() << "[DirectorSession] Failed to initialize decoder.";
    //     m_stream.reset();
    //     return;
    // }

    m_readfuture = QtConcurrent::run([this]() {
        readLoop();
    });

}

void DirectorSession::detachTrack() {
    if (m_stream) {
        m_stream->close();
        m_stream.reset();
    }
    if (m_readfuture.isRunning()) {
        m_readfuture.waitForFinished();
    }
}

void DirectorSession::readLoop() {
    livekit::VideoFrameEvent event;
    
    while (m_stream && m_stream->read(event)) {
        // const uint8_t *data = event.frame.data();
        // size_t size = event.frame.dataSize();

        // auto pkt = std::make_unique<videoCore::Packet>();
        // pkt->packet.reset(av_packet_alloc());
        // av_new_packet(pkt->packet.get(), static_cast<int>(size));
        // memcpy(pkt->packet->data, data, size);

        // m_decoder->decodePacket(std::move(pkt));

        // TODO: push videoframe object to framereader

    }
}