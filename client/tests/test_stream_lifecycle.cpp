/*
 * test_stream_lifecycle.cpp
 *
 * End-to-end lifecycle test: real camera → H.264 encoder → WHIP publisher
 * → LiveKit ingress → director frame reception.
 *
 * Requires the prod docker stack:
 *   docker compose -f docker-compose.prod.yaml up -d
 *
 * Requires a V4L2 camera at /dev/video0 that supports MJPEG at 1280×720@30fps.
 *
 * Usage:
 *   ./build/test_stream_lifecycle [signaling_url]
 *
 * Default signaling_url: http://localhost:50051
 *
 * What to watch for in the output:
 *   - "Keyframe encoded" lines confirm the GOP cadence is correct.
 *   - Periodic stats show whether encoded frames are reaching the director.
 *   - "First frame received" confirms end-to-end delivery is working.
 *   - A low receive ratio (< 0.5) with no publisher error points to a
 *     transport / LiveKit ingress problem rather than an encoding bug.
 *   - "[WHIPPublisher] GStreamer error" lines identify pipeline failures.
 *   - "[v4l2] The driver changed the time per frame" means the camera does
 *     not support MJPEG at 1280×720@30fps — try a lower resolution.
 *   - "[h264 @ ...] Frame num change" means RTP packets were lost; the PLI
 *     handler should trigger an IDR within one frame.
 */

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#include <atomic>

#include <gst/gst.h>

#include "livekit/livekit.h"

#include "directortransport.hpp"
#include "directorsession.hpp"
#include "videotrack.hpp"
#include "sessionclient.hpp"

#include "capture/capture_config.hpp"
#include "encode/encoder_config.hpp"
#include "pipeline/video_pipeline.hpp"
#include "whip_publisher.hpp"

// ---------------------------------------------------------------------------
// Test parameters
// ---------------------------------------------------------------------------

static constexpr int kWidth      = 1280;
static constexpr int kHeight     = 720;
static constexpr int kFramerate  = 30;
static constexpr int kGopSize    = 30;    // 1-second keyframe interval
static constexpr int kBitrate    = 4000000;
static constexpr int kStreamSecs = 20;
static constexpr int kReportMs   = 5000;  // stats log interval

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) {
        fprintf(stderr, "%s\n", qPrintable(msg));
        fflush(stderr);
    });

    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true"));
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    const QString serverUrl = (app.arguments().size() > 1)
        ? app.arguments().at(1)
        : QStringLiteral("http://localhost:50051");

    // ---- Stats (written from multiple threads) -----------------------------
    std::atomic<int>  framesEncoded{0};
    std::atomic<int>  keyframesEncoded{0};
    std::atomic<int>  framesReceived{0};
    std::atomic<bool> publisherError{false};
    std::atomic<bool> firstFrameLogged{false};
    // ------------------------------------------------------------------------

    SessionClient                  client;
    DirectorTransport              transport;
    networking::WHIPPublisher      publisher;
    videoCore::pipeline::VideoPipeline pipeline;

    QString roomCode, livekitToken, livekitUrl, whipUrl, streamKey;

    QObject::connect(&client, &SessionClient::error, &app, [](const QString &msg) {
        qCritical() << "[lifecycle] Signaling error:" << msg;
    });

    qDebug() << "[lifecycle] Connecting to signaling:" << serverUrl;
    client.connectToServer(QUrl(serverUrl));

    // --- Session creation ---------------------------------------------------
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::sessionCreated, &loop,
                         [&](const QString &code) { roomCode = code; loop.quit(); });
        client.createSession("lifecycle-director", 1);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (roomCode.isEmpty()) {
        qCritical() << "[lifecycle] Timed out waiting for session creation. Is the docker stack up?";
        return 1;
    }
    qDebug() << "[lifecycle] Session created:" << roomCode;

    // --- Director join ------------------------------------------------------
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::directorJoined, &loop,
                         [&](const QString &t, const QString &u) {
                             livekitToken = t;
                             livekitUrl   = u;
                             loop.quit();
                         });
        client.joinSession(roomCode, "lifecycle-director", "director");
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (livekitToken.isEmpty()) {
        qCritical() << "[lifecycle] Timed out waiting for director join.";
        return 1;
    }
    qDebug() << "[lifecycle] Director joined. LiveKit URL:" << livekitUrl;

    // --- Operator join ------------------------------------------------------
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::cameraJoined, &loop,
                         [&](const QString &url, const QString &key) {
                             whipUrl   = url;
                             streamKey = key;
                             loop.quit();
                         });
        client.joinSession(roomCode, "lifecycle-operator", "camera");
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (whipUrl.isEmpty()) {
        qCritical() << "[lifecycle] Timed out waiting for operator join.";
        return 1;
    }
    qDebug() << "[lifecycle] Operator joined. WHIP URL:" << whipUrl;

    // --- Director: connect to LiveKit and arm frame counter -----------------
    QVideoSink directorSink;
    QObject::connect(&directorSink, &QVideoSink::videoFrameChanged, &app,
                     [&](const QVideoFrame &frame) {
                         const int n = framesReceived.fetch_add(1, std::memory_order_relaxed) + 1;

                         // Log first frame and every 30th frame.
                         if (n == 1 || (n % 30 == 0)) {
                             QVideoFrame mf = frame;
                             const bool mapped = mf.map(QVideoFrame::ReadOnly);

                             qDebug() << "[lifecycle] Qt frame #" << n
                                      << frame.width() << "x" << frame.height()
                                      << "pixelFormat:" << frame.pixelFormat()
                                      << "bytesPerLine(0):" << (mapped ? mf.bytesPerLine(0) : -1)
                                      << "mappedBytes(0):" << (mapped ? mf.mappedBytes(0) : -1)
                                      << "expectedRGBA:" << (frame.width() * frame.height() * 4);

                             if (mapped && n == 1) {
                                 // Sample RGBA at 5 positions to reveal actual pixel content.
                                 // Layout: [R,G,B,A] per pixel, row-major.
                                 const int w = frame.width();
                                 const int h = frame.height();
                                 const int stride = mf.bytesPerLine(0);
                                 const uint8_t *d = mf.bits(0);
                                 auto sample = [&](int x, int y) {
                                     const uint8_t *p = d + y * stride + x * 4;
                                     qDebug() << "[lifecycle] pixel (" << x << "," << y << ")"
                                              << "R=" << (int)p[0]
                                              << "G=" << (int)p[1]
                                              << "B=" << (int)p[2]
                                              << "A=" << (int)p[3];
                                 };
                                 sample(0,        0);           // top-left
                                 sample(w/2,      0);           // top-center
                                 sample(w-1,      0);           // top-right
                                 sample(w/2,      h/2);         // center
                                 sample(w/2,      h-1);         // bottom-center

                                 // Write frame as PPM to /tmp/frame_dump.ppm so it
                                 // can be inspected with any image viewer.
                                 // Format: P6 header + raw RGB (skip alpha byte).
                                 FILE *ppm = fopen("/tmp/frame_dump.ppm", "wb");
                                 if (ppm) {
                                     fprintf(ppm, "P6\n%d %d\n255\n", w, h);
                                     for (int row = 0; row < h; ++row) {
                                         const uint8_t *src = d + row * stride;
                                         for (int col = 0; col < w; ++col) {
                                             fwrite(src + col * 4, 1, 3, ppm); // R,G,B only
                                         }
                                     }
                                     fclose(ppm);
                                     qDebug() << "[lifecycle] First frame written to /tmp/frame_dump.ppm";
                                 }
                             }

                             if (mapped) mf.unmap();

                             if (n == 1 && !firstFrameLogged.exchange(true)) {
                                 qDebug() << "[lifecycle] First frame received (see above for format details)";
                             }
                         }
                     });

    {
        bool connected = false;
        QEventLoop loop;
        QObject::connect(&transport, &DirectorTransport::connected, &loop, [&]() {
            connected = true;
            if (transport.session() != nullptr) {
                // Tracks are not yet attached at connect time — the operator
                // starts publishing after the director joins. Wire up the sink
                // when the first track arrives. Use &app as context so this
                // connection outlives the inner event loop.
                QObject::connect(transport.session(), &DirectorSession::trackAdded,
                                 &app, [&](qsizetype index) {
                    const QList<QObject *> tracks = transport.session()->tracks();
                    if (index < tracks.size()) {
                        if (auto *vt = qobject_cast<VideoTrack *>(tracks.at(index))) {
                            vt->setVideoSink(&directorSink);
                        }
                    }
                });
            }
            loop.quit();
        });
        transport.connectToRoom(livekitToken, livekitUrl);
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        if (!connected) {
            qCritical() << "[lifecycle] Timed out waiting for LiveKit room connection.";
            client.closeSession(roomCode, "lifecycle-director");
            return 1;
        }
    }
    qDebug() << "[lifecycle] Director connected to LiveKit room.";

    // --- Operator: initialize camera capture + encoder pipeline -------------
    videoCore::capture::CaptureConfig capConfig;
#ifdef _WIN32
    capConfig.devicePath  = "video=0";
    capConfig.inputFormat = "dshow";
#else
    capConfig.devicePath  = "/dev/video0";
    capConfig.inputFormat = "v4l2";
    // Most USB cameras can only sustain 30 fps at 720p in MJPEG; raw formats
    // (YUYV, NV12) typically cap at 10 fps at this resolution.
    capConfig.pixelFormat = "mjpeg";
#endif
    capConfig.width     = kWidth;
    capConfig.height    = kHeight;
    capConfig.framerate = kFramerate;

    videoCore::encode::EncoderConfig encConfig;
    encConfig.width     = kWidth;
    encConfig.height    = kHeight;
    encConfig.framerate = kFramerate;
    encConfig.bitrate   = kBitrate;
    encConfig.gopSize   = kGopSize;
    encConfig.preset    = videoCore::encode::EncoderConfig::Preset::UltraFast;
    encConfig.type      = videoCore::encode::EncoderConfig::Type::Software;

    if (pipeline.initialize(capConfig, encConfig) != videoCore::Result::Success) {
        qCritical() << "[lifecycle] Video pipeline initialization failed."
                    << "Is a camera at /dev/video0?";
        transport.disconnectFromRoom();
        client.closeSession(roomCode, "lifecycle-director");
        return 1;
    }

    // --- Operator: initialize WHIP publisher --------------------------------
    const auto pubInitResult = publisher.initialize(
        whipUrl.toStdString(), streamKey.toStdString(), kFramerate,
        [&](const std::string &err) {
            qCritical() << "[lifecycle] Publisher error:" << QString::fromStdString(err);
            publisherError.store(true, std::memory_order_relaxed);
        });
    if (pubInitResult != networking::Result::Success) {
        qCritical() << "[lifecycle] WHIPPublisher initialization failed.";
        transport.disconnectFromRoom();
        client.closeSession(roomCode, "lifecycle-director");
        return 1;
    }

    // Wire PLI/FIR: when the remote decoder reports packet loss, the publisher
    // receives a GstForceKeyUnitEvent and calls back here to request an IDR.
    publisher.setKeyframeRequestCallback([&pipeline]() {
        pipeline.requestKeyframe();
    });

    qDebug() << "[lifecycle] Starting WHIP handshake (blocks up to 20s)...";
    if (publisher.start() != networking::Result::Success) {
        qCritical() << "[lifecycle] WHIP publisher failed to start."
                    << "Check: docker compose -f docker-compose.prod.yaml logs ingress";
        transport.disconnectFromRoom();
        client.closeSession(roomCode, "lifecycle-director");
        return 1;
    }
    qDebug() << "[lifecycle] WHIP publisher started.";

    // --- Start camera → encoder → publisher chain ---------------------------
    const auto pipeStartResult = pipeline.start(
        [&](std::unique_ptr<videoCore::Packet> pkt) {
            framesEncoded.fetch_add(1, std::memory_order_relaxed);
            if (pkt->isKeyframe) {
                const int kf = keyframesEncoded.fetch_add(1, std::memory_order_relaxed) + 1;
                qDebug() << "[lifecycle] Keyframe #" << kf
                         << "at encoded frame" << framesEncoded.load()
                         << "pts_ns=" << pkt->pts;
            }
            publisher.pushPacket(std::move(pkt));
        });
    if (pipeStartResult != videoCore::Result::Success) {
        qCritical() << "[lifecycle] Video pipeline start failed.";
        publisher.stop();
        transport.disconnectFromRoom();
        client.closeSession(roomCode, "lifecycle-director");
        return 1;
    }
    qDebug() << "[lifecycle] Streaming for" << kStreamSecs << "seconds...";

    // --- Stream with periodic stats -----------------------------------------
    const int iterations = (kStreamSecs * 1000) / kReportMs;
    for (int i = 0; i < iterations; ++i) {
        QEventLoop loop;
        QTimer::singleShot(kReportMs, &loop, &QEventLoop::quit);
        loop.exec();

        const int elapsed = (i + 1) * kReportMs / 1000;
        qDebug() << "[lifecycle] --- t=" << elapsed << "s ---"
                 << " encoded="    << framesEncoded.load()
                 << " keyframes="  << keyframesEncoded.load()
                 << " received="   << framesReceived.load()
                 << " dropped="    << pipeline.getDroppedFrames()
                 << " publisherError=" << publisherError.load();
    }

    // --- Teardown -----------------------------------------------------------
    qDebug() << "[lifecycle] Stopping pipeline...";
    if (pipeline.stop() != videoCore::Result::Success) {
        qWarning() << "[lifecycle] Pipeline stop returned an error.";
    }
    publisher.stop();
    transport.disconnectFromRoom();

    // --- Final report -------------------------------------------------------
    const int enc  = framesEncoded.load();
    const int kf   = keyframesEncoded.load();
    const int rcv  = framesReceived.load();
    const int expectedFrames    = kStreamSecs * kFramerate;
    const int expectedKeyframes = kStreamSecs * kFramerate / kGopSize;

    qDebug() << "\n[lifecycle] ===== Final Report =====";
    qDebug() << "  Frames encoded  :" << enc  << "(expected ~" << expectedFrames << ")";
    qDebug() << "  Keyframes       :" << kf   << "(expected ~" << expectedKeyframes << ")";
    qDebug() << "  Frames dropped  :" << pipeline.getDroppedFrames() << "(queue overflow)";
    qDebug() << "  Frames received :" << rcv;
    qDebug() << "  Publisher error :" << publisherError.load();

    if (enc == 0) {
        qCritical() << "  RESULT: FAIL — encoder produced no frames. Check camera and MJPEG support.";
    } else if (kf == 0) {
        qCritical() << "  RESULT: FAIL — no keyframes encoded; check gopSize and encoder config.";
    } else if (rcv == 0) {
        qCritical() << "  RESULT: FAIL — no frames received on director side."
                    << "Check LiveKit ingress logs and WHIP handshake.";
    } else {
        // Compare against actual encoded frames, not the theoretical max.
        // The camera may deliver fewer frames than kFramerate (e.g. 10fps on a
        // USB webcam that only supports MJPEG at 10fps at 1280×720).
        const float ratio = static_cast<float>(rcv) / static_cast<float>(enc);
        qDebug() << "  Receive ratio   :" << ratio
                 << "(received / encoded) | < 0.5 suggests transport or ingress issues";
        if (ratio >= 0.5f) {
            qDebug() << "  RESULT: PASS (frames flowing end-to-end)";
        } else {
            qWarning() << "  RESULT: WARN — low receive ratio; check LiveKit ingress and ICE.";
        }
    }

    // --- Close session ------------------------------------------------------
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::sessionClosed, &loop,
                         [&](bool) { loop.quit(); });
        client.closeSession(roomCode, "lifecycle-director");
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    qDebug() << "[lifecycle] Done.";
    livekit::shutdown();
    gst_deinit();
    return (rcv > 0) ? 0 : 1;
}
