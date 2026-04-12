#include "session_controller.hpp"

#include <QDebug>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QtConcurrent/QtConcurrent>
#include <cstring>

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

    // Wire up preview callback
    if (previewSink_ != nullptr) {
        QVideoSink *sink = previewSink_;
        session_->setPreviewCallback([sink](const videoCore::Frame &frame) {
            if (frame.frame == nullptr) {
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

    startFuture_ = QtConcurrent::run([this, std_url, std_key]() {
        qDebug() << "[CameraSessionController] Starting session.";
        return session_->start(std_url, std_key);
    });

    startWatcher_->setFuture(startFuture_);
}

void CameraSessionController::stop() {
    session_->stop();
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