#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QFuture>
#include <QFutureWatcher>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVideoSink>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include "camera_session.hpp"

class CameraSessionController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVideoSink *previewSink READ previewSink WRITE setPreviewSink NOTIFY previewSinkChanged)
    // The camera that start() will use.  Empty string means "auto-pick the
    // default camera at session start".  Set this from QML before start();
    // changing it after the session is running has no effect until restart.
    Q_PROPERTY(QString selectedCamera READ selectedCamera WRITE setSelectedCamera NOTIFY selectedCameraChanged)

public:
    explicit CameraSessionController(QObject *parent = nullptr);
    ~CameraSessionController() override;

    static CameraSessionController *create(QQmlEngine *engine, QJSEngine *) {
        return new CameraSessionController(engine);
    }

    Q_INVOKABLE void start(const QString &whipUrl, const QString &streamKey);
    Q_INVOKABLE void stop();

    // Enumerate available cameras.  Returns a list of QVariantMap entries
    // with keys: "id" (string, opaque identifier), "name" (display name),
    // "source" (e.g. "v4l2", "pipewire").  Safe to call from QML; performs
    // a fresh GstDeviceMonitor scan on each invocation.
    Q_INVOKABLE QVariantList listCameras();

    QVideoSink *previewSink() const;
    void setPreviewSink(QVideoSink *sink);

    QString selectedCamera() const { return selectedCameraId_; }
    void setSelectedCamera(const QString &id);

signals:
    void sessionStarted();
    void sessionStopped();
    void errorOccurred(const QString &message);
    void previewSinkChanged();
    void selectedCameraChanged();
    // Emitted on the main thread for each captured frame. captureNs is the
    // local wall-clock nanoseconds at the moment the preview callback fired.
    void frameCaptured(qint64 captureNs);
    // Emitted once per SENDER_DELAY_POLL_INTERVAL_MS while the session is
    // running, carrying WHIPPublisher's latest send-pad-probe rolling mean.
    // Wired by QML / test_e2e_latency into CameraLatencySender::setSenderDelayMs
    // so the next DC packet's v2 payload carries the value to the director.
    void senderDelayMsChanged(double delayMs);

private:
    std::shared_ptr<CameraSession> session_;
    QFuture<bool> startFuture_;
    QFutureWatcher<bool> *startWatcher_ = nullptr;
    QVideoSink *previewSink_ = nullptr;
    QString selectedCameraId_;  // empty → auto-pick at start time

    // Maps a pipeline-clock pts (ns) to the wall-clock capture_ns recorded in
    // the preview callback at that frame.  Looked up from the encode thread
    // when a packet is produced so frameCaptured fires at the encoder output
    // rate (== transmit rate) rather than the raw capture rate, keeping DC
    // and video frame rates aligned at the receiver for accurate FIFO match.
    //
    // The encoder rescales pts: ns → (1/fps timebase, integer) → ns, which is
    // lossy.  Lookup uses nearest-match within ENCODER_PTS_TOLERANCE_NS to
    // tolerate that round-trip rather than failing silently and starving DCs.
    // Per-frame capture timestamps in two clock domains.  capture_wall_ns is
    // system_clock — the value the camera ships over the data channel so the
    // director can compute dc_one_way against the camera's offset-corrected
    // wall clock.  capture_steady_ns is steady_clock — used for any interval
    // local to this machine (encode pipeline, UI hop) so an NTP/`chronyd`
    // step on the wall clock between capture and packet emit doesn't corrupt
    // the diagnostic numbers.
    struct CaptureTimes {
        qint64 wall_ns;
        qint64 steady_ns;
    };

    std::mutex pts_to_capture_ns_mutex_;
    std::map<int64_t, CaptureTimes> pts_to_capture_ns_;
    static constexpr std::size_t MAX_PTS_MAP_ENTRIES = 240; // ~4 s at 60 fps
    // Half of one frame interval at 30 fps; encoder rescaling rounds to the
    // nearest integer in 1/fps units, so a 16 ms tolerance is generous.
    static constexpr int64_t ENCODER_PTS_TOLERANCE_NS = 16'700'000;

    std::atomic<std::uint64_t> packet_lookup_hits_{0};
    std::atomic<std::uint64_t> packet_lookup_misses_{0};

    // Polls WHIPPublisher's send-pad-probe reading once per second and
    // forwards it via senderDelayMsChanged for QML/test wiring.
    QTimer *senderDelayTimer_ = nullptr;
    static constexpr int SENDER_DELAY_POLL_INTERVAL_MS = 1000;
};