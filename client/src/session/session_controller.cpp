#include "session_controller.hpp"

#include <QDebug>
#include <QtConcurrent/QtConcurrent>

CameraSessionController::CameraSessionController(QObject *parent)
    : QObject(parent)
    , session_(std::make_shared<CameraSession>()) {}

CameraSessionController::~CameraSessionController() {
    if (m_startFuture.isRunning()) {
        m_startFuture.waitForFinished();
    }
}

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
            qDebug() << "[CameraSessionController] The session has started.";
        }
        else {
            emit errorOccurred("Failed to start camera session");
            qWarning() << "[CameraSessionController] Could not start session.";
        }
    });

    std::shared_ptr<CameraSession> session = session_;

    m_startFuture = QtConcurrent::run([this, std_url, std_key]() {
        qDebug() << "[CameraSessionController] Starting session.";
        return session_->start(std_url, std_key);
    });

    m_startWatcher->setFuture(m_startFuture);
}

void CameraSessionController::stop() {
    session_->stop();
    emit sessionStopped();
}