#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QFutureWatcher>
#include "camera_session.hpp"

class CameraSessionController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit CameraSessionController(QObject *parent = nullptr);

    static CameraSessionController *create(QQmlEngine *, QJSEngine *) {
        static CameraSessionController instance;
        return &instance;
    }

    Q_INVOKABLE void start(const QString &whipUrl, const QString &streamKey);
    Q_INVOKABLE void stop();

signals:
    void sessionStarted();
    void sessionStopped();
    void errorOccurred(const QString &message);

private:
    std::shared_ptr<CameraSession> session_;
    QFutureWatcher<bool> *m_startWatcher = nullptr;
};