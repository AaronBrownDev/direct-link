#include "session_controller.hpp"
#include "../../../video-core/include/capture/camera_enumerator.hpp"

#include <QDebug>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>

CameraSessionController::CameraSessionController(QObject *parent)
    : QObject(parent)
    , session_(std::make_shared<CameraSession>()) {}

CameraSessionController::~CameraSessionController() {
    if (startFuture_.isRunning()) {
        startFuture_.waitForFinished();
    }
}

void CameraSessionController::start(const QString &whipUrl,
                                     const QString &streamKey) {
    if ((startWatcher_ != nullptr) && startWatcher_->isRunning()) {
        return;
    }

    std::string std_url = whipUrl.toStdString();
    std::string std_key = streamKey.toStdString();

    // Preview callback runs on the capture thread once per incoming raw
    // frame.  We do two things here:
    //   1) Record the wall-clock capture_ns keyed by the frame's pipeline
    //      pts.  The matching pts comes back to us out of the encoder, and
    //      that's where we actually emit frameCaptured (so DC sends are
    //      paced by the encoder's output rate, not the camera's bursty
    //      capture rate — see CameraSession::setPacketEncodedCallback below).
    //   2) Render to the optional preview sink for the operator UI.
    {
        QVideoSink *sink = previewSink_;
        QPointer<CameraSessionController> self(this);
        session_->setPreviewCallback([sink, self](const videoCore::Frame &frame) {
            // Two clocks paired at the same instant.  Wall is sent over the DC
            // (cross-machine — director needs it to apply clock_offset).  Steady
            // is used for any interval that stays on this machine (encode
            // pipeline, UI hop) so a wall-clock step between capture and emit
            // doesn't fold into the diagnostic numbers.
            const qint64 capture_wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const qint64 capture_steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            if (self) {
                std::lock_guard<std::mutex> lock(self->pts_to_capture_ns_mutex_);
                if (self->pts_to_capture_ns_.size() >= MAX_PTS_MAP_ENTRIES) {
                    // Encoder is dropping faster than we can prune; remove
                    // oldest to bound memory.  Should be rare in practice.
                    self->pts_to_capture_ns_.erase(self->pts_to_capture_ns_.begin());
                }
                self->pts_to_capture_ns_[frame.pts] = {capture_wall_ns, capture_steady_ns};
            }

            if (sink == nullptr || frame.frame == nullptr) {
                return;
            }
            QVideoFrameFormat fmt(QSize(frame.width, frame.height),
                                  QVideoFrameFormat::Format_YUV420P);
            QVideoFrame qFrame(fmt);
            if (!qFrame.map(QVideoFrame::WriteOnly)) {
                return;
            }
            for (int p = 0; p < 3; ++p) {
                const int srcStride = frame.frame->linesize[p];
                const int dstStride = qFrame.bytesPerLine(p);
                const int planeH    = (p == 0) ? frame.height : frame.height / 2;
                const int copyW     = std::min(srcStride, dstStride);
                const auto *src     = frame.frame->data[p];
                auto       *dst     = qFrame.bits(p);
                for (int row = 0; row < planeH; ++row) {
                    std::memcpy(
                        dst + static_cast<std::ptrdiff_t>(row) * dstStride,
                        src + static_cast<std::ptrdiff_t>(row) * srcStride,
                        static_cast<std::size_t>(copyW));
                }
            }
            qFrame.unmap();
            sink->setVideoFrame(qFrame);
        });
    }

    // Encoder-output callback runs on the encode thread once per packet.
    // Look up the original capture_ns by the packet's pts.  The encoder
    // rescales ns → (1/fps integer) → ns, which rounds, so an exact find()
    // would silently miss most packets and starve the DC stream — we use
    // nearest-match within ENCODER_PTS_TOLERANCE_NS instead.  Anything older
    // than the matched entry is now orphaned (its frame was dropped before
    // encode) and is erased to keep the map bounded.
    {
        QPointer<CameraSessionController> self(this);
        session_->setPacketEncodedCallback([self](int64_t pts) {
            if (!self) { return; }
            CaptureTimes capture_times{};
            bool found = false;
            std::int64_t matched_pts_diff = 0;
            {
                std::lock_guard<std::mutex> lock(self->pts_to_capture_ns_mutex_);
                if (!self->pts_to_capture_ns_.empty()) {
                    // Find the entry whose key is closest to pts.  lower_bound
                    // gives the first key >= pts; the candidate just before
                    // it (if any) might be closer.
                    auto upper = self->pts_to_capture_ns_.lower_bound(pts);
                    auto best  = self->pts_to_capture_ns_.end();
                    std::int64_t best_diff = std::numeric_limits<std::int64_t>::max();
                    if (upper != self->pts_to_capture_ns_.end()) {
                        const std::int64_t diff = std::abs(upper->first - pts);
                        if (diff < best_diff) { best = upper; best_diff = diff; }
                    }
                    if (upper != self->pts_to_capture_ns_.begin()) {
                        auto prev = std::prev(upper);
                        const std::int64_t diff = std::abs(prev->first - pts);
                        if (diff < best_diff) { best = prev; best_diff = diff; }
                    }
                    if (best != self->pts_to_capture_ns_.end()
                        && best_diff <= ENCODER_PTS_TOLERANCE_NS) {
                        capture_times = best->second;
                        matched_pts_diff = best->first - pts;
                        found = true;
                        self->pts_to_capture_ns_.erase(self->pts_to_capture_ns_.begin(),
                                                        std::next(best));
                    }
                }
            }
            if (found) {
                self->packet_lookup_hits_.fetch_add(1, std::memory_order_relaxed);
                // Profile: capture → encoder packet output (encode_pipeline_ms)
                // and capture → dispatched-to-UI (encode_to_dispatch_ms).  The
                // latter is the moment we ask the UI thread to fire frameCaptured;
                // the actual publishData call happens after one Qt event-loop hop.
                // Both intervals come from steady_clock so a wall-clock step
                // can't masquerade as encoder lag.
                const qint64 packet_steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                const qint64 encode_pipeline_ms = (packet_steady_ns - capture_times.steady_ns) / 1'000'000;
                const auto hits = self->packet_lookup_hits_.load(std::memory_order_relaxed);
                if (hits <= 5 || (hits % 60) == 0) {
                    qDebug().nospace()
                        << "[CSC-diag] enc pkt hit#" << hits
                        << " pts=" << pts
                        << " encode_pipeline_ms=" << encode_pipeline_ms
                        << " matched_pts_diff_ns=" << matched_pts_diff
                        << " misses=" << self->packet_lookup_misses_.load(std::memory_order_relaxed);
                }
                const qint64 capture_wall_ns = capture_times.wall_ns;
                const qint64 capture_steady_ns = capture_times.steady_ns;
                QMetaObject::invokeMethod(self, [self, capture_wall_ns, capture_steady_ns]() {
                    if (!self) { return; }
                    const qint64 dispatch_steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    const qint64 ui_hop_ms = (dispatch_steady_ns - capture_steady_ns) / 1'000'000;
                    static std::atomic<std::uint64_t> ui_hop_log_count{0};
                    const auto n = ui_hop_log_count.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (n <= 5 || (n % 60) == 0) {
                        qDebug().nospace()
                            << "[CSC-diag] frame#" << n
                            << " ui_hop_ms=" << ui_hop_ms
                            << " total_capture_to_emit_ms=" << ((dispatch_steady_ns - capture_steady_ns) / 1'000'000);
                    }
                    emit self->frameCaptured(capture_wall_ns);
                }, Qt::QueuedConnection);
            } else {
                const auto misses = self->packet_lookup_misses_.fetch_add(1, std::memory_order_relaxed) + 1;
                if (misses <= 5 || (misses % 30) == 0) {
                    qWarning().nospace()
                        << "[CSC-diag] enc pkt MISS#" << misses
                        << " pts=" << pts
                        << " (no map entry within tolerance — DC will not fire for this frame)";
                }
            }
        });
    }

    startWatcher_ = new QFutureWatcher<bool>(this);

    connect(startWatcher_, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool success = startWatcher_->result();
        startWatcher_->deleteLater();
        startWatcher_ = nullptr;

        if (success) {
            emit sessionStarted();
            qDebug() << "[CameraSessionController] The session has started.";
        }
        else {
            emit errorOccurred("Failed to start camera session");
            qWarning() << "[CameraSessionController] Could not start session.";
        }
    });

    std::shared_ptr<CameraSession> session = session_;
    std::string std_device_id = selectedCameraId_.toStdString();

    startFuture_ = QtConcurrent::run([this, std_url, std_key, std_device_id]() {
        qDebug() << "[CameraSessionController] Starting session.";
        return session_->start(std_url, std_key, std_device_id);
    });

    startWatcher_->setFuture(startFuture_);
}

QVariantList CameraSessionController::listCameras() {
    QVariantList result;
    for (const auto &device :
         videoCore::capture::CameraEnumerator::listDevices()) {
        QVariantMap entry;
        entry["id"] = QString::fromStdString(device.id);
        entry["name"] = QString::fromStdString(device.displayName);
        entry["source"] = QString::fromStdString(device.source);
        // Light summary so a UI can show "1280x720 @ 30fps + 4 more" without
        // a second round-trip; full caps remain in C++ for now.
        entry["formatCount"] = static_cast<int>(device.formats.size());
        result.append(entry);
    }
    return result;
}

void CameraSessionController::setSelectedCamera(const QString &id) {
    if (selectedCameraId_ == id) {
        return;
    }
    selectedCameraId_ = id;
    emit selectedCameraChanged();
}

void CameraSessionController::stop() {
    // CameraSession::stop blocks waiting for the WHIP pipeline to flush —
    // EOS propagation through whipsink triggers an HTTP DELETE to the WHIP
    // ingress whose handler can take several seconds.  Run that on a worker
    // thread so the QML thread (which calls stop() from
    // Component.onDestruction when the user leaves the session page) isn't
    // frozen for the duration.
    //
    // session_ is held by shared_ptr; capturing a copy keeps it alive across
    // the worker even if a subsequent start() has already replaced our local
    // reference.  Pipeline construction/teardown inside CameraSession is
    // serialised by its own state, so concurrent start+stop on the same
    // session would still be undefined — but the controller serializes
    // through QFutureWatcher anyway.
    auto session = session_;
    // QThreadPool::start (rather than QtConcurrent::run) — we don't need the
    // QFuture, and ignoring it triggers a [[nodiscard]] warning.
    QThreadPool::globalInstance()->start([session]() { session->stop(); });
    emit sessionStopped();
}

QVideoSink *CameraSessionController::previewSink() const {
    return previewSink_;
}

void CameraSessionController::setPreviewSink(QVideoSink *sink) {
    if (previewSink_ == sink) {
        return;
    }
    previewSink_ = sink;
    emit previewSinkChanged();
}