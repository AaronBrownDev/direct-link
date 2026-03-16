#include "directortransport.hpp"

#include <QDebug>

static const char* disconnectReasonToString(livekit::DisconnectReason reason) {
    switch (reason) {
        case livekit::DisconnectReason::ClientInitiated:    return "ClientInitiated";
        case livekit::DisconnectReason::ConnectionTimeout:  return "ConnectionTimeout";
        case livekit::DisconnectReason::DuplicateIdentity:  return "DuplicateIdentity";
        case livekit::DisconnectReason::JoinFailure:        return "JoinFailure";
        case livekit::DisconnectReason::MediaFailure:       return "MediaFailure";
        case livekit::DisconnectReason::Migration:          return "Migration";
        case livekit::DisconnectReason::ParticipantRemoved: return "ParticipantRemoved";
        case livekit::DisconnectReason::RoomClosed:         return "RoomClosed";
        case livekit::DisconnectReason::RoomDeleted:        return "RoomDeleted";
        case livekit::DisconnectReason::ServerShutdown:     return "ServerShutdown";
        case livekit::DisconnectReason::SignalClose:        return "SignalClose";
        case livekit::DisconnectReason::SipTrunkFailure:    return "SipTrunkFailure";
        case livekit::DisconnectReason::StateMismatch:      return "StateMismatch";
        case livekit::DisconnectReason::Unknown:            return "Unknown";
        case livekit::DisconnectReason::UserRejected:       return "UserRejected";
        case livekit::DisconnectReason::UserUnavailable:    return "UserUnavailable";
        default:                                            return "<unrecognised>";
    }
}

DirectorTransport::DirectorTransport(QObject *parent) : QObject(parent), m_room(std::make_unique<livekit::Room>()) {
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
    m_room->setDelegate(this);
}

DirectorTransport::~DirectorTransport() {
    m_room.reset();
    livekit::shutdown();
}

void DirectorTransport::onParticipantConnected(livekit::Room &, const livekit::ParticipantConnectedEvent &event) {
    qDebug() << "[DirectorTransport] Participant connected.\n\tidentity="
                << event.participant->identity()
                << "\n\tname="
                << event.participant->name() << "\n";
}

void DirectorTransport::onTrackSubscribed(livekit::Room &, const livekit::TrackSubscribedEvent &event) {
    const char *participant_id = event.participant ? event.participant->identity().c_str() : "<unknown>";
    const std::string track_sid = event.publication ? event.publication->sid() : "<unknown>";
    const std::string track_name = event.publication ? event.publication->name() : "<unknown>";

    qDebug() << "[DirectorTransport] Track subscribed.\n\tparticipant_id=" << participant_id
             << "\n\ttrack_sid=" << track_sid
             << "\n\ttrack_name=" << track_name;

    if (event.track) {
        qDebug() << "kind=" << static_cast<int>(event.track->kind());
    }

    if (event.publication) {
        qDebug() << "source=" << static_cast<int>(event.publication->source());
    }

    qDebug() << "\n";

    //  TODO: Attach track to DirectorSession in QMetaObject
}

void DirectorTransport::onTrackSubscriptionFailed(livekit::Room &, const livekit::TrackSubscriptionFailedEvent &event) {
    const char *participant_id = event.participant ? event.participant->identity().c_str() : "<unknown>";
    const std::string msg = event.error;
    const std::string track_sid = event.track_sid;

    qDebug() << "[DirectorTransport] Failed to subscribe to track."
             << "\n\tparticipant_id=" << participant_id
             << "\n\ttrack_sid=" << track_sid
             << "\n\terror=" << msg;
}

void DirectorTransport::onTrackUnsubscribed(livekit::Room &, const livekit::TrackUnsubscribedEvent &event) {
    const char *participant_id = event.participant ? event.participant->identity().c_str() : "<unknown>";
    const std::string track_sid = event.publication ? event.publication->sid() : "<unknown>";
    const std::string track_name = event.publication ? event.publication->name() : "<unknown>";
    
    qDebug() << "[DirectorTransport] Track unsubscribed.\n\tparticipant_id=" << participant_id
             << "\n\ttrack_sid=" << track_sid
             << "\n\ttrack_name=" << track_name;

    if (event.track) {
        qDebug() << "kind=" << static_cast<int>(event.track->kind());
    }

    if (event.publication) {
        qDebug() << "source=" << static_cast<int>(event.publication->source());
    }

    qDebug() << "\n";

    // TODO: Detach track from DirectorSession in QMetaObject
}

void DirectorTransport::onConnectionStateChanged(livekit::Room &, const livekit::ConnectionStateChangedEvent &event) {
    livekit::ConnectionState state = event.state;

    QMetaObject::invokeMethod(this, [this, state]() {
        switch (state) {
        case livekit::ConnectionState::Connected:
            m_connection_state = "connected";
            qDebug() << "[DirectorTransport] Connection Status: connected";
            break;
        case livekit::ConnectionState::Reconnecting:
            m_connection_state = "reconnecting";
            qDebug() << "[DirectorTransport] Connection Status: reconnecting";
            break;
        case livekit::ConnectionState::Disconnected:
            m_connection_state = "disconnected";
            qDebug() << "[DirectorTransport] Connection Status: disconnected";
            break;
        default:
            break;
    }

    emit connectionStateChanged(m_connection_state);
    }, Qt::QueuedConnection);

}

void DirectorTransport::onDisconnected(livekit::Room &, const livekit::DisconnectedEvent &event) {
    livekit::DisconnectReason reason = event.reason;

    qDebug() << "[DirectorTransport] Disconnected."
             << "\n\treason=" << disconnectReasonToString(reason);

    QMetaObject::invokeMethod(this, [this]() {
        emit disconnected();
    }, Qt::QueuedConnection);
}

void DirectorTransport::connectToRoom(const QString &token, const QString &url) {
    livekit::RoomOptions opts;
    bool success;

    qDebug() << "[DirectorTransport] Connecting to room."
             << "\n\ttoken=" << token
             << "\n\turl=" << url;
    
    if (!m_room) {
        m_room = std::make_unique<livekit::Room>();
        m_room->setDelegate(this);
    }

    success = m_room->Connect(url.toStdString(), token.toStdString(), opts);

    if (success) {
        qDebug() << "[DirectorTransport] Connected.";
        emit connected();
    }
    else
    {
        qWarning() << "[DirectorTransport] Connection failed.";
    }
}

void DirectorTransport::disconnectFromRoom() {
    if (m_connection_state == "disconnected") return;
    qDebug() << "[DirectorTransport] Disconnecting...";
    m_room.reset();
    emit disconnected();
}

QString DirectorTransport::connectionState() const {
    return m_connection_state;
}
