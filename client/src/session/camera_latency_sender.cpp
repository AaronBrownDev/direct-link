#include "camera_latency_sender.hpp"

#include <QDebug>
#include <livekit/local_participant.h>
#include <livekit/room.h>

#include <chrono>
#include <cstdint>

CameraLatencySender::CameraLatencySender(QObject *parent)
    : QObject(parent)
    , m_room(nullptr)
    , m_timer(new QTimer(this))
{
    // 33 ms fallback — one tick per video frame at ~30 fps. Replaced by
    // accurate per-frame timestamps once onFrameCaptured is wired up.
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &CameraLatencySender::sendTimestamp);
}

CameraLatencySender::~CameraLatencySender() {
    stop();
}

void CameraLatencySender::start(const QString &token, const QString &url) {
    if (token.isEmpty() || url.isEmpty()) {
        qWarning() << "[CameraLatencySender] Empty credentials, not starting.";
        return;
    }

    if ((m_connectWatcher != nullptr) && m_connectWatcher->isRunning()) {
        qWarning() << "[CameraLatencySender] Connection already in progress.";
        return;
    }

    m_room = std::make_unique<livekit::Room>();

    // Data-only participant: do not auto-subscribe to any media tracks.
    // single_peer_connection forces the v1 signal path, which makes the SDK
    // call publisher_negotiation_needed() at session start. Without it the
    // Rust SDK skips the initial publisher offer for data-only participants
    // when subscriber_primary=false (livekit-ffi v0.12.x bug).
    livekit::RoomOptions opts;
    opts.auto_subscribe = false;
    opts.single_peer_connection = true;

    std::string std_token = token.toStdString();
    std::string std_url = url.toStdString();

    m_connectWatcher = new QFutureWatcher<bool>(this);

    connect(m_connectWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool success = m_connectWatcher->result();
        m_connectWatcher->deleteLater();
        m_connectWatcher = nullptr;

        if (!m_room) {
            // stop() was called before connect completed
            return;
        }

        if (success) {
            qDebug() << "[CameraLatencySender] Connected to LiveKit room.";
            m_timer->start();
            emit connected();
        } else {
            qWarning() << "[CameraLatencySender] Failed to connect to LiveKit room.";
            m_room.reset();
            emit connectionFailed();
        }
    });

    QFuture<bool> future = QtConcurrent::run([this, std_url, std_token, opts]() mutable {
        return m_room->Connect(std_url, std_token, opts);
    });
    m_connectWatcher->setFuture(future);
}

void CameraLatencySender::stop() {
    m_timer->stop();
    m_frame_driven = false;
    // Resetting the room before the connect watcher signals prevents the
    // finished lambda from starting the timer after stop() returns.
    m_room.reset();
}

void CameraLatencySender::onFrameCaptured(qint64 captureNs) {
    if (!m_frame_driven) {
        m_frame_driven = true;
        m_timer->stop();
        qDebug() << "[CLS-diag] switched to frame-driven DC sends "
                    "(timer fallback stopped)";
    }
    if (!m_room || !m_room->localParticipant()) {
        return;
    }
    const auto n = m_frame_driven_sends.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 5 || (n % 60) == 0) {
        qDebug().nospace()
            << "[CLS-diag] frame-driven send#" << n
            << " timer-driven=" << m_timer_driven_sends.load(std::memory_order_relaxed);
    }
    sendTimestampNs(captureNs + m_clock_offset_ns.load(std::memory_order_relaxed));
}

void CameraLatencySender::setClockOffset(qint64 offsetNs) {
    m_clock_offset_ns.store(offsetNs, std::memory_order_relaxed);
}

void CameraLatencySender::sendTimestamp() {
    if (!m_room || !m_room->localParticipant()) {
        return;
    }
    const qint64 local_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto n = m_timer_driven_sends.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 5 || (n % 60) == 0) {
        qDebug().nospace() << "[CLS-diag] timer-driven send#" << n;
    }
    sendTimestampNs(local_ns + m_clock_offset_ns.load(std::memory_order_relaxed));
}

void CameraLatencySender::sendTimestampNs(qint64 serverNs) {
    // Serialize as big-endian int64 (8 bytes).
    std::vector<uint8_t> payload(8);
    qint64 tmp = serverNs;
    for (int i = 7; i >= 0; --i) {
        payload[static_cast<std::size_t>(i)] = static_cast<uint8_t>(tmp & 0xFF);
        tmp >>= 8;
    }
    try {
        // reliable=true: latency timestamps are tiny (8 bytes) and infrequent
        // enough that the cost of TCP-style retransmit is negligible, but
        // packet loss directly distorts the FIFO match in DirectorTransport
        // (queue stays at MAX_CAPTURE_QUEUE_SIZE while DC arrival rate at
        // the receiver drops below the video frame rate, which falsely
        // inflates video_lag_ms — observed as ~6000 ms over Ethernet).
        m_room->localParticipant()->publishData(payload, /*reliable=*/true, {}, "latency");
    } catch (const std::exception &e) {
        qWarning() << "[CameraLatencySender] publishData failed:" << e.what();
    }
}
