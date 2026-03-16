#include "session_controller.hpp"


CameraSessionController::CameraSessionController(QObject *parent)
    : QObject(parent) {}

void CameraSessionController::start(const QString &whipUrl,
                                     const QString &streamKey) {
    if (!session_.start(whipUrl.toStdString(), streamKey.toStdString())) {
        emit errorOccurred("Failed to start camera session");
    }
    emit sessionStarted();
}

void CameraSessionController::stop() {
    session_.stop();
}