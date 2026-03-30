#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <thread>
#include <tuple>

#include <gst/gst.h>

#include "capture/capture_config.hpp"
#include "encode/encoder_config.hpp"
#include "pipeline/video_pipeline.hpp"
#include "session_controller.hpp"
#include "sessionclient.hpp"

// ---------------------------------------------------------------------------
// Local camera-only test
//
// Tests the full capture → encode pipeline without a signaling server or WHIP
// infrastructure. Useful for diagnosing camera capture issues locally.
//
// Usage:  ./build/test_operator_session --local
// Exit 0  — at least one encoded frame was produced
// Exit 1  — pipeline failed to start or produced no frames
// ---------------------------------------------------------------------------
static int runLocalCameraTest() {
    gst_init(nullptr, nullptr);

    videoCore::capture::CaptureConfig capConfig;
    capConfig.devicePath  = "/dev/video0";
    capConfig.inputFormat = "v4l2";
    capConfig.pixelFormat = "mjpeg";
    capConfig.width       = 1280;
    capConfig.height      = 720;
    capConfig.framerate   = 30;

    videoCore::encode::EncoderConfig encConfig;
    encConfig.width     = capConfig.width;
    encConfig.height    = capConfig.height;
    encConfig.framerate = capConfig.framerate;
    encConfig.bitrate   = 4000000;
    encConfig.gopSize   = 30;
    encConfig.preset    = videoCore::encode::EncoderConfig::Preset::UltraFast;
    encConfig.type      = videoCore::encode::EncoderConfig::Type::Software;

    videoCore::pipeline::VideoPipeline pipeline;

    qDebug() << "[camera-test] Initializing pipeline...";
    const auto initResult = pipeline.initialize(capConfig, encConfig);
    if (initResult != videoCore::Result::Success) {
        qCritical() << "[camera-test] FAIL — initialize:"
                    << QString::fromStdString(
                           std::string(videoCore::resultToString(initResult)));
        gst_deinit();
        return 1;
    }

    std::atomic<int> framesEncoded{0};
    const auto startResult = pipeline.start(
        [&](std::unique_ptr<videoCore::Packet>) {
            framesEncoded.fetch_add(1, std::memory_order_relaxed);
        });

    if (startResult != videoCore::Result::Success) {
        qCritical() << "[camera-test] FAIL — start:"
                    << QString::fromStdString(
                           std::string(videoCore::resultToString(startResult)));
        gst_deinit();
        return 1;
    }

    qDebug() << "[camera-test] Capturing for 5 seconds...";
    for (int i = 1; i <= 5; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        qDebug() << "[camera-test] t=" << i << "s  encoded=" << framesEncoded.load()
                 << " dropped=" << pipeline.getDroppedFrames();
    }

    std::ignore = pipeline.stop();

    const int frames = framesEncoded.load();
    if (frames > 0) {
        qDebug() << "[camera-test] PASS —" << frames << "frames encoded";
        gst_deinit();
        return 0;
    }

    qCritical() << "[camera-test] FAIL — no frames encoded after 5 s";
    gst_deinit();
    return 1;
}

// ---------------------------------------------------------------------------
// Full integration test (requires signaling server + LiveKit/WHIP stack)
//
// Usage:  ./build/test_operator_session [signaling_url]
// Default signaling_url: http://localhost:50051
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);

    if (app.arguments().contains("--local")) {
        return runLocalCameraTest();
    }

    const QString serverUrl = (app.arguments().size() > 1)
        ? app.arguments().at(1)
        : QStringLiteral("http://localhost:50051");

    SessionClient client;
    CameraSessionController controller;

    QString roomCode;
    QString whipUrl;
    QString streamKey;
    bool sessionStarted = false;

    QEventLoop createLoop;
    QEventLoop directorLoop;
    QEventLoop operatorLoop;
    QEventLoop startLoop;
    QEventLoop closeLoop;

    QObject::connect(&client, &SessionClient::error, &app, [](const QString &msg) {
        qCritical() << "[test] Signaling error:" << msg;
    });

    qDebug() << "[test] Connecting to" << serverUrl;
    client.connectToServer(QUrl(serverUrl));

    // ------------------------------------
    // SESSION CREATION
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionCreated, &createLoop,
                     [&](const QString &code) {
                         roomCode = code;
                         createLoop.quit();
                     });

    qDebug() << "[test] Creating session...";
    client.createSession("director-test", 1);
    createLoop.exec();

    Q_ASSERT(!roomCode.isEmpty());
    qDebug() << "[test] Session created:" << roomCode;

    // ------------------------------------
    // DIRECTOR JOIN (establishes the session on the server)
    // ------------------------------------

    QObject::connect(&client, &SessionClient::directorJoined, &directorLoop,
                     [&](const QString &, const QString &) {
                         directorLoop.quit();
                     });

    qDebug() << "[test] Director joining...";
    client.joinSession(roomCode, "director-test", "director");
    directorLoop.exec();

    qDebug() << "[test] Director joined.";

    // ------------------------------------
    // OPERATOR JOIN
    // ------------------------------------

    QObject::connect(&client, &SessionClient::cameraJoined, &operatorLoop,
                     [&](const QString &url, const QString &key) {
                         whipUrl = url;
                         streamKey = key;
                         operatorLoop.quit();
                     });

    qDebug() << "[test] Operator joining...";
    client.joinSession(roomCode, "operator-test", "camera");
    operatorLoop.exec();

    Q_ASSERT(!whipUrl.isEmpty());
    Q_ASSERT(!streamKey.isEmpty());
    qDebug() << "[test] Operator joined. WHIP URL:" << whipUrl;

    // ------------------------------------
    // CAMERA SESSION START
    // ------------------------------------

    QObject::connect(&controller, &CameraSessionController::sessionStarted,
                     &startLoop, [&]() {
                         sessionStarted = true;
                         startLoop.quit();
                     });

    QObject::connect(&controller, &CameraSessionController::errorOccurred,
                     &startLoop, [&](const QString &msg) {
                         qCritical() << "[test] Camera session error:" << msg;
                         startLoop.quit();
                     });

    qDebug() << "[test] Starting camera session...";
    controller.start(whipUrl, streamKey);

    // 25s — covers the 20s gst_element_get_state timeout plus startup
    QTimer::singleShot(25000, &startLoop, &QEventLoop::quit);
    startLoop.exec();

    if (!sessionStarted) {
        qCritical() << "[test] Camera session failed to start. Aborting.";
        client.closeSession(roomCode, "director-test");
        QEventLoop abortClose;
        QObject::connect(&client, &SessionClient::sessionClosed, &abortClose,
                         [&](bool) { abortClose.quit(); });
        abortClose.exec();
        gst_deinit();
        return 1;
    }

    qDebug() << "[test] Camera session started. Streaming for 10 seconds...";

    // ------------------------------------
    // STREAM
    // ------------------------------------

    QEventLoop streamLoop;
    QTimer::singleShot(10000, &streamLoop, &QEventLoop::quit);
    streamLoop.exec();

    // ------------------------------------
    // STOP
    // ------------------------------------

    qDebug() << "[test] Stopping camera session...";
    controller.stop();
    qDebug() << "[test] Camera session stopped.";

    // ------------------------------------
    // CLOSE SESSION
    // ------------------------------------

    QObject::connect(&client, &SessionClient::sessionClosed, &closeLoop,
                     [&](bool success) {
                         if (!success) {
                             qWarning() << "[test] Session close reported failure.";
                         }
                         closeLoop.quit();
                     });

    qDebug() << "[test] Closing session...";
    client.closeSession(roomCode, "director-test");
    closeLoop.exec();

    qDebug() << "[test] Done.";
    gst_deinit();
    return 0;
}
