#include "directortransport.hpp"

#include <QDebug>
#include <livekit/remote_participant.h>
#include <livekit/remote_track_publication.h>
#include <livekit/room.h>

#include <chrono>

static std::string_view disconnectReasonToString(livekit::DisconnectReason reason) {
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

void DirectorTransport::onParticipantConnected(livekit::Room & /*unused*/, const livekit::ParticipantConnectedEvent &event) {
    qDebug() << "[DirectorTransport] Participant connected.\n\tidentity="
                << event.participant->identity()
                << "\n\tname="
                << event.participant->name() << "\n";
}

void DirectorTransport::onTrackSubscribed(livekit::Room & /*unused*/, const livekit::TrackSubscribedEvent &event) {
    const char *participant_id = (event.participant != nullptr) ? event.participant->identity().c_str() : "<unknown>";
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

    if (event.track && event.track->kind() == livekit::TrackKind::KIND_VIDEO) {
        auto track = event.track;
        QMetaObject::invokeMethod(this, [this, track, track_sid]() {
        if (m_session) {
            m_session->attachTrack(track, track_sid);
        }
        }, Qt::QueuedConnection);
        
    }

}

void DirectorTransport::onTrackSubscriptionFailed(livekit::Room & /*unused*/, const livekit::TrackSubscriptionFailedEvent &event) {
    const char *participant_id = (event.participant != nullptr) ? event.participant->identity().c_str() : "<unknown>";
    const std::string msg = event.error;
    const std::string track_sid = event.track_sid;

    qWarning() << "[DirectorTransport] Failed to subscribe to track."
             << "\n\tparticipant_id=" << participant_id
             << "\n\ttrack_sid=" << track_sid
             << "\n\terror=" << msg;

    QMetaObject::invokeMethod(this, [this, track_sid]() {
        if (m_session) {
            m_session->detachTrack(track_sid);
        }
    }, Qt::QueuedConnection);
}

void DirectorTransport::onTrackUnsubscribed(livekit::Room & /*unused*/, const livekit::TrackUnsubscribedEvent &event) {
    const char *participant_id = (event.participant != nullptr) ? event.participant->identity().c_str() : "<unknown>";
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

    QMetaObject::invokeMethod(this, [this, track_sid]() {
        if (m_session) {
            m_session->detachTrack(track_sid);
        }
    }, Qt::QueuedConnection);
}

void DirectorTransport::onConnectionStateChanged(livekit::Room & /*unused*/, const livekit::ConnectionStateChangedEvent &event) {
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

    if (m_connection_state == new_state) { return; }

    m_connection_state = new_state;
    qDebug() << "[DirectorTransport] Connection status changed.\n\tstatus=" << m_connection_state;
    emit connectionStateChanged(m_connection_state);
    }, Qt::QueuedConnection);

}

void DirectorTransport::onUserPacketReceived(livekit::Room & /*unused*/, const livekit::UserDataPacketEvent &event) {
    if (event.topic != "latency" || event.data.size() != 8) {
        return;
    }

    // Deserialize big-endian int64 — capture time in server clock domain.
    qint64 capture_ns = 0;
    for (int i = 0; i < 8; ++i) {
        capture_ns = (capture_ns << 8) | static_cast<qint64>(event.data[static_cast<std::size_t>(i)]);
    }

    // Stamp the local wall-clock time the DC packet arrived; used to separate
    // network+encode latency from jitter-buffer+decode latency in onFrameArrived.
    const qint64 dc_arrived_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Enqueue for consumption by onFrameArrived when the corresponding decoded
    // video frame arrives. Drop the oldest entry if the queue is full (e.g.
    // video pipeline stalled).
    std::lock_guard<std::mutex> lock(m_capture_queue_mutex);
    if (m_capture_queue.size() >= MAX_CAPTURE_QUEUE_SIZE) {
        m_capture_queue.pop();
    }
    m_capture_queue.push({capture_ns, dc_arrived_ns});
}

void DirectorTransport::onFrameArrived(qint64 receivedNs) {
    // Always record for display-gap sampling even if no timestamp is queued.
    m_last_received_ns = receivedNs;
    m_frame_pending = true;

    CaptureEntry entry{};
    {
        std::lock_guard<std::mutex> lock(m_capture_queue_mutex);
        if (m_capture_queue.empty()) { return; }
        entry = m_capture_queue.front();
        m_capture_queue.pop();
    }

    const qint64 offset = m_clock_offset_ns.load(std::memory_order_relaxed);

    // dc_one_way: camera preview callback → DC packet arrived at director.
    // Both sides expressed in server clock domain, so clock offset applied once.
    const double dc_one_way_ms = static_cast<double>(entry.dc_arrived_ns + offset - entry.capture_ns) / 1e6;

    // video_lag: DC packet arrived → video frame decoded (encode pipeline +
    // jitter buffer + decode). Both timestamps are director local clock so
    // the offset cancels.
    const double video_lag_ms = static_cast<double>(receivedNs - entry.dc_arrived_ns) / 1e6;

    const double gap_ms = displayGapMs();
    const double total_ms = dc_one_way_ms + video_lag_ms + gap_ms;

    qDebug() << "[DirectorTransport] Latency breakdown:"
             << "\n\tdc_one_way=" << dc_one_way_ms << "ms"
             << "\n\tvideo_lag=" << video_lag_ms << "ms"
             << "\n\tdisplay_gap=" << gap_ms << "ms"
             << "\n\ttotal=" << total_ms << "ms"
             << "\n\tclock_offset=" << (static_cast<double>(offset) / 1'000'000.0) << "ms";

    if (total_ms > 0.0 && total_ms < 30000.0) {
        emit latencyMeasured(total_ms);
        emit latencyBreakdown(dc_one_way_ms, video_lag_ms, gap_ms);
    }
}

void DirectorTransport::onFrameSwapped() {
    if (!m_frame_pending) { return; }
    m_frame_pending = false;

    const qint64 now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const qint64 gap_ns = now_ns - m_last_received_ns;

    // Sanity bounds: ignore gaps outside 0–500 ms (stale frames, system hiccup).
    if (gap_ns <= 0 || gap_ns > 500'000'000LL) { return; }

    m_display_gap_sum_ns -= gsl::at(m_display_gap_buf, m_display_gap_idx);
    gsl::at(m_display_gap_buf, m_display_gap_idx) = gap_ns;
    m_display_gap_sum_ns += gap_ns;
    m_display_gap_idx = (m_display_gap_idx + 1) % DISPLAY_GAP_SAMPLES;
    if (m_display_gap_count < DISPLAY_GAP_SAMPLES) { ++m_display_gap_count; }
}

void DirectorTransport::onDisconnected(livekit::Room & /*unused*/, const livekit::DisconnectedEvent &event) {
    livekit::DisconnectReason reason = event.reason;

    qDebug() << "[DirectorTransport] Disconnected from room."
             << "\n\treason=" << disconnectReasonToString(reason);

    QMetaObject::invokeMethod(this, [this]() {
        emit disconnected();
    }, Qt::QueuedConnection);
}

void DirectorTransport::connectToRoom(const QString &token, const QString &url) {
    if ((m_connectWatcher != nullptr) && m_connectWatcher->isRunning()) {
        qWarning() << "[DirectorTransport] Connection already in progress.";
        return;
    }

    if (token.isEmpty() || url.isEmpty()) {
        qWarning() << "[DirectorTransport] Empty room credentials given. Could not connect.";
        return;
    }

    livekit::RoomOptions opts;
    std::string std_url = url.toStdString();
    std::string std_token = token.toStdString();

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
            connect(m_session.get(), &DirectorSession::frameArrived,
                    this, &DirectorTransport::onFrameArrived);
            qDebug() << "[DirectorTransport] Connected.";
            emit sessionChanged();
            emit connected();
        }
        else
        {
            qWarning() << "[DirectorTransport] Connection failed.";
        }
    });

    QFuture<bool> future = QtConcurrent::run([this, std_url, std_token, opts]() mutable {
        return m_room->Connect(std_url, std_token, opts);
    });
    
    m_connectWatcher->setFuture(future);
}

void DirectorTransport::disconnectFromRoom() {
    if (m_connection_state == "disconnected") { return; }
    shutdown();
    m_connection_state = "disconnected";
    qDebug() << "[DirectorTransport] Disconnected.";
    emit connectionStateChanged(m_connection_state);
    emit sessionChanged();
    emit disconnected();
}

void DirectorTransport::shutdown() {
    m_session.reset();
    m_room.reset();
    std::lock_guard<std::mutex> lock(m_capture_queue_mutex);
    while (!m_capture_queue.empty()) { m_capture_queue.pop(); }
}

void DirectorTransport::setClockOffset(qint64 ns) {
    m_clock_offset_ns.store(ns, std::memory_order_relaxed);
}

void DirectorTransport::setWindow(QObject *window) {
    auto *qw = qobject_cast<QQuickWindow *>(window);
    if (m_window != nullptr) {
        disconnect(m_window, &QQuickWindow::frameSwapped, this, &DirectorTransport::onFrameSwapped);
    }
    m_window = qw;
    if (m_window != nullptr) {
        connect(m_window, &QQuickWindow::frameSwapped, this, &DirectorTransport::onFrameSwapped, Qt::QueuedConnection);
    }
}

double DirectorTransport::displayGapMs() const {
    if (m_display_gap_count == 0) { return 0.0; }
    return static_cast<double>(m_display_gap_sum_ns) / m_display_gap_count / 1e6;
}

QString DirectorTransport::connectionState() const {
    return m_connection_state;
}

DirectorSession *DirectorTransport::session() const {
    return m_session.get();
}