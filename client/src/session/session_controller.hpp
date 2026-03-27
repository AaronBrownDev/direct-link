#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QFuture>
#include <QFutureWatcher>
#include <memory>
#include "camera_session.hpp"

class CameraSessionController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit CameraSessionController(QObject *parent = nullptr);
    ~CameraSessionController() override;

    static CameraSessionController *create(QQmlEngine *engine, QJSEngine *) {
        return new CameraSessionController(engine);
    }

    Q_INVOKABLE void start(const QString &whipUrl, const QString &streamKey);
    Q_INVOKABLE void stop();

signals:
    void sessionStarted();
    void sessionStopped();
    void errorOccurred(const QString &message);

private:
    std::shared_ptr<CameraSession> session_;
    QFuture<bool> m_startFuture;
    QFutureWatcher<bool> *m_startWatcher = nullptr;
};