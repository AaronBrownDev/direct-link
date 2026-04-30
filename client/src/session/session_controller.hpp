#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QFuture>
#include <QFutureWatcher>
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

public:
    explicit CameraSessionController(QObject *parent = nullptr);
    ~CameraSessionController() override;

    static CameraSessionController *create(QQmlEngine *engine, QJSEngine *) {
        return new CameraSessionController(engine);
    }

    Q_INVOKABLE void start(const QString &whipUrl, const QString &streamKey);
    Q_INVOKABLE void stop();

    QVideoSink *previewSink() const;
    void setPreviewSink(QVideoSink *sink);

signals:
    void sessionStarted();
    void sessionStopped();
    void errorOccurred(const QString &message);
    void previewSinkChanged();
    // Emitted on the main thread for each captured frame. captureNs is the
    // local wall-clock nanoseconds at the moment the preview callback fired.
    void frameCaptured(qint64 captureNs);

private:
    std::shared_ptr<CameraSession> session_;
    QFuture<bool> startFuture_;
    QFutureWatcher<bool> *startWatcher_ = nullptr;
    QVideoSink *previewSink_ = nullptr;

    // Maps a pipeline-clock pts (ns) to the wall-clock capture_ns recorded in
    // the preview callback at that frame.  Looked up from the encode thread
    // when a packet is produced so frameCaptured fires at the encoder output
    // rate (== transmit rate) rather than the raw capture rate, keeping DC
    // and video frame rates aligned at the receiver for accurate FIFO match.
    //
    // The encoder rescales pts: ns → (1/fps timebase, integer) → ns, which is
    // lossy.  Lookup uses nearest-match within ENCODER_PTS_TOLERANCE_NS to
    // tolerate that round-trip rather than failing silently and starving DCs.
    std::mutex pts_to_capture_ns_mutex_;
    std::map<int64_t, qint64> pts_to_capture_ns_;
    static constexpr std::size_t MAX_PTS_MAP_ENTRIES = 240; // ~4 s at 60 fps
    // Half of one frame interval at 30 fps; encoder rescaling rounds to the
    // nearest integer in 1/fps units, so a 16 ms tolerance is generous.
    static constexpr int64_t ENCODER_PTS_TOLERANCE_NS = 16'700'000;

    std::atomic<std::uint64_t> packet_lookup_hits_{0};
    std::atomic<std::uint64_t> packet_lookup_misses_{0};
};