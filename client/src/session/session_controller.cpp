#include "session_controller.hpp"

#include <QDebug>
#include <QtConcurrent/QtConcurrent>

CameraSessionController::CameraSessionController(QObject *parent)
    : QObject(parent) {}

void CameraSessionController::start(const QString &whipUrl,
                                     const QString &streamKey) {
    if ((m_startWatcher != nullptr) && m_startWatcher->isRunning()) {
        return;
    }

    std::string std_url = whipUrl.toStdString();
    std::string std_key = streamKey.toStdString();

    m_startWatcher = new QFutureWatcher<bool>(this);

    connect(m_startWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool success = m_startWatcher->result();
        m_startWatcher->deleteLater();
        m_startWatcher = nullptr;

        if (success) {
            emit sessionStarted();
        }
        else {
            emit errorOccurred("Failed to start camera session");
        }
    });

    QFuture<bool> future = QtConcurrent::run([this, std_url, std_key]() {
        return session_.start(std_url, std_key);
    });

    m_startWatcher->setFuture(future);
}

void CameraSessionController::stop() {
    session_.stop();
    emit sessionStopped();
}