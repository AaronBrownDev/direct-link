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

DirectorTransport::DirectorTransport(QObject *parent) : QObject(parent) {}

DirectorTransport::~DirectorTransport() {
    shutdown();
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

    if (event.track && event.track->kind() == livekit::TrackKind::KIND_VIDEO) {
        auto track = event.track;
        QMetaObject::invokeMethod(this, [this, track]() {
        if (m_session) {
            m_session->attachTrack(track);
        }
        }, Qt::QueuedConnection);
        
    }

}

void DirectorTransport::onTrackSubscriptionFailed(livekit::Room &, const livekit::TrackSubscriptionFailedEvent &event) {
    const char *participant_id = event.participant ? event.participant->identity().c_str() : "<unknown>";
    const std::string msg = event.error;
    const std::string track_sid = event.track_sid;

    qWarning() << "[DirectorTransport] Failed to subscribe to track."
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

    QMetaObject::invokeMethod(this, [this]() {
        if (m_session) {
            // Currently takes no args and detaches the track last attached.
            m_session->detachTrack();
        }
    }, Qt::QueuedConnection);
}

void DirectorTransport::onConnectionStateChanged(livekit::Room &, const livekit::ConnectionStateChangedEvent &event) {
    livekit::ConnectionState state = event.state;

    QMetaObject::invokeMethod(this, [this, state]() {
        QString new_state;

        switch (state) {
        case livekit::ConnectionState::Connected:
            new_state = "connected";
            break;
        case livekit::ConnectionState::Reconnecting:
            new_state = "connecting";
            break;
        case livekit::ConnectionState::Disconnected:
            new_state = "disconnected";
            break;
        default:
            break;
    }

    if (m_connection_state == new_state) return;

    m_connection_state = new_state;
    qDebug() << "[DirectorTransport] Connection status changed.\n\tstatus=" << m_connection_state;
    emit connectionStateChanged(m_connection_state);
    }, Qt::QueuedConnection);

}

void DirectorTransport::onDisconnected(livekit::Room &, const livekit::DisconnectedEvent &event) {
    livekit::DisconnectReason reason = event.reason;

    qDebug() << "[DirectorTransport] Disconnected from room."
             << "\n\treason=" << disconnectReasonToString(reason);

    QMetaObject::invokeMethod(this, [this]() {
        emit disconnected();
    }, Qt::QueuedConnection);
}

void DirectorTransport::connectToRoom(const QString &token, const QString &url) {
    if (m_connectWatcher && m_connectWatcher->isRunning()) {
        qWarning() << "[DirectorTransport] Connection already in progress.";
        return;
    }

    if (token.isEmpty() || url.isEmpty()) {
        qWarning() << "[DirectorTransport] Empty room credentials given. Could not connect.";
        return;
    }

    livekit::RoomOptions opts;
    std::string stdUrl = url.toStdString();
    std::string stdToken = token.toStdString();

    m_connection_state = "connecting";
    emit connectionStateChanged(m_connection_state);    

    qDebug() << "[DirectorTransport] Connecting to room."
             << "\n\ttoken=" << token
             << "\n\turl=" << url;
    
    if (!m_room) {
        m_room = std::make_unique<livekit::Room>();
        m_room->setDelegate(this);
    }


    // LiveKit's connect function blocks the thread it is in
    // Put it in a QFuture so the app does not freeze
    m_connectWatcher = new QFutureWatcher<bool>(this);

    connect(m_connectWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool success = m_connectWatcher->result();
        m_connectWatcher->deleteLater();
        m_connectWatcher = nullptr;

        if (m_connection_state == "disconnected") {
            qWarning() << "[DirectorTransport] Connection finished after disconnect; cancelling.";
            return;
        }

        if (success) {
            m_session = std::make_unique<DirectorSession>();
            qDebug() << "[DirectorTransport] Connected.";
            emit sessionChanged();
            emit connected();
        }
        else
        {
            qWarning() << "[DirectorTransport] Connection failed.";
        }
    });

    QFuture<bool> future = QtConcurrent::run([this, stdUrl, stdToken, opts]() mutable {
        return m_room->Connect(stdUrl, stdToken, opts);
    });
    
    m_connectWatcher->setFuture(future);
}

void DirectorTransport::disconnectFromRoom() {
    if (m_connection_state == "disconnected") return;
    m_session.reset();
    m_room.reset();
    m_connection_state = "disconnected";
    qDebug() << "[DirectorTransport] Disconnected.";
    emit connectionStateChanged(m_connection_state);
    emit sessionChanged();
    emit disconnected();
}

void DirectorTransport::shutdown() {
    m_session.reset();
    m_room.reset();
}

QString DirectorTransport::connectionState() const {
    return m_connection_state;
}

DirectorSession *DirectorTransport::session() const {
    return m_session.get();
}