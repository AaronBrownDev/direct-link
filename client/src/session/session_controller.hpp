#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QFuture>
#include <QFutureWatcher>
#include <QVideoSink>
#include <memory>
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
};