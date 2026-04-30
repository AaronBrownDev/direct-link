// test_e2e_latency.cpp
//
// Full pipeline E2E latency test against local docker-compose.prod.yaml.
// Requires: a real camera on /dev/video0, docker-compose running locally.
//
// Usage: ./test_e2e_latency [--server <url>] [--samples <n>]
//   --server  Signaling gRPC URL  (default: http://localhost:50051)
//   --samples Samples to collect  (default: 30)
//
// Output per sample:
//   [Sample N/total]  dc=Xms  vid=Xms  gap=Xms  total=Xms
//
// dc  = camera preview callback → data-channel packet arrived at director
//       (encode latency + network; fast path, no jitter buffer)
// vid = data-channel packet arrived → video frame decoded
//       (LiveKit jitter buffer + video decode; the dominant latency source)
// gap = video frame decoded → QQuickWindow swap (0 in headless test)

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <gst/gst.h>

#include "livekit/livekit.h"
#include "sessionclient.hpp"
#include "session_controller.hpp"
#include "camera_latency_sender.hpp"
#include "directortransport.hpp"

#include <algorithm>
#include <numeric>
#include <vector>

struct Sample {
    double dc_ms;
    double vid_ms;
    double gap_ms;
    double total_ms;
};

static void printStats(const std::vector<Sample> &s) {
    if (s.empty()) {
        qDebug() << "[Stats] No samples.";
        return;
    }

    auto stat = [&](auto fn) {
        std::vector<double> v;
        v.reserve(s.size());
        for (const auto &x : s) v.push_back(fn(x));
        std::sort(v.begin(), v.end());
        double mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
        double p95  = v[static_cast<size_t>(static_cast<double>(v.size()) * 0.95)];
        return std::make_tuple(v.front(), mean, v[v.size() / 2], p95, v.back());
    };

    auto row = [](const char *label, auto t) {
        auto [mn, mean, p50, p95, mx] = t;
        qDebug().nospace()
            << "  " << label
            << "  min=" << static_cast<int>(mn) << "ms"
            << "  mean=" << static_cast<int>(mean) << "ms"
            << "  p50=" << static_cast<int>(p50) << "ms"
            << "  p95=" << static_cast<int>(p95) << "ms"
            << "  max=" << static_cast<int>(mx) << "ms";
    };

    qDebug() << "\n[Stats] N=" << s.size();
    row("dc_one_way ", stat([](const auto &x) { return x.dc_ms; }));
    row("video_lag  ", stat([](const auto &x) { return x.vid_ms; }));
    row("display_gap", stat([](const auto &x) { return x.gap_ms; }));
    row("total      ", stat([](const auto &x) { return x.total_ms; }));
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);
    livekit::initialize(livekit::LogLevel::Warn, livekit::LogSink::kConsole);

    QCommandLineParser parser;
    parser.setApplicationDescription("E2E latency test — requires local docker-compose");
    parser.addHelpOption();
    parser.addOption({"server",  "Signaling gRPC URL",      "url", "http://localhost:50051"});
    parser.addOption({"samples", "Number of samples",        "n",   "30"});
    parser.process(app);

    const QUrl   server_url    = QUrl(parser.value("server"));
    const int    target        = parser.value("samples").toInt();

    SessionClient          client;
    DirectorTransport      director;
    CameraSessionController camera;
    CameraLatencySender    sender;

    QString room_code, dir_token, livekit_url;
    QString whip_url, stream_key, data_token, data_url;
    std::vector<Sample> samples;
    int exit_code = 1;

    QObject::connect(&client, &SessionClient::error, &app, [](const QString &msg) {
        qCritical() << "[Test] Signaling error:" << msg;
    });
    QObject::connect(&camera, &CameraSessionController::errorOccurred, &app, [](const QString &msg) {
        qCritical() << "[Test] Camera error:" << msg;
    });

    // ── 1. Create session ────────────────────────────────────────────────────
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::sessionCreated, &loop,
            [&](const QString &c) { room_code = c; loop.quit(); });
        client.connectToServer(server_url);
        client.createSession("e2e-director", 1);
        loop.exec();
    }
    Q_ASSERT(!room_code.isEmpty());
    qDebug() << "[Test] Session:" << room_code;

    // ── 2. Director join ─────────────────────────────────────────────────────
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::directorJoined, &loop,
            [&](const QString &tok, const QString &url) {
                dir_token = tok; livekit_url = url; loop.quit();
            });
        client.joinSession(room_code, "e2e-director", "director");
        loop.exec();
    }
    Q_ASSERT(!dir_token.isEmpty());

    // ── 3. Camera join ───────────────────────────────────────────────────────
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::cameraJoined, &loop,
            [&](const QString &wu, const QString &sk, const QString &dt, const QString &du) {
                whip_url = wu; stream_key = sk; data_token = dt; data_url = du; loop.quit();
            });
        client.joinSession(room_code, "e2e-camera", "camera");
        loop.exec();
    }
    Q_ASSERT(!whip_url.isEmpty());

    // ── 4. Clock sync (3 rounds, same-machine offset ≈ 0 but good practice) ──
    for (int i = 0; i < 3; ++i) {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::clockOffsetChanged, &loop, &QEventLoop::quit);
        client.measureLatency();
        loop.exec();
        director.setClockOffset(client.clockOffsetNs());
        sender.setClockOffset(client.clockOffsetNs());
    }
    qDebug() << "[Test] Clock offset:" << (client.clockOffsetNs() / 1e6) << "ms";

    // ── 5. Director connects to LiveKit ──────────────────────────────────────
    {
        QEventLoop loop;
        bool ok = false;
        QObject::connect(&director, &DirectorTransport::connected, &loop,
            [&]() { ok = true; loop.quit(); });
        director.connectToRoom(dir_token, livekit_url);
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        loop.exec();
        if (!ok) {
            qCritical() << "[Test] Director failed to connect within 20s.";
            goto cleanup;
        }
    }
    qDebug() << "[Test] Director connected.";

    // The latency matcher filters DCs and frames by active participant so a
    // single-camera test must register the camera's identity, otherwise no
    // breakdown signal will ever fire.
    director.setActiveParticipant("e2e-camera");

    // ── 6. Wire breakdown signal before camera starts ────────────────────────
    {
        QEventLoop sample_loop;
        QObject::connect(&director, &DirectorTransport::latencyBreakdown,
            &app, [&](double dc, double vid, double gap) {
                Sample s{dc, vid, gap, dc + vid + gap};
                samples.push_back(s);
                qDebug().nospace()
                    << "[Sample " << samples.size() << "/" << target << "]"
                    << "  dc="    << static_cast<int>(dc)  << "ms"
                    << "  vid="   << static_cast<int>(vid) << "ms"
                    << "  gap="   << static_cast<int>(gap) << "ms"
                    << "  total=" << static_cast<int>(dc + vid + gap) << "ms";
                if (static_cast<int>(samples.size()) >= target) sample_loop.quit();
            });

        // ── 7. Start camera (WHIP publish) ───────────────────────────────────
        {
            QEventLoop loop;
            bool cam_ok = false;
            QObject::connect(&camera, &CameraSessionController::sessionStarted, &loop,
                [&]() { cam_ok = true; loop.quit(); });

            // Wire frame timestamps before starting so the sender is frame-driven
            // from the very first captured frame.
            QPointer<CameraLatencySender> sp(&sender);
            QObject::connect(&camera, &CameraSessionController::frameCaptured,
                &sender, [sp](qint64 captureNs) {
                    if (sp) sp->onFrameCaptured(captureNs);
                });

            camera.start(whip_url, stream_key);
            QTimer::singleShot(25000, &loop, &QEventLoop::quit);
            loop.exec();
            if (!cam_ok) {
                qCritical() << "[Test] Camera session failed to start within 25s.";
                goto cleanup;
            }
        }
        qDebug() << "[Test] Camera session started.";

        // ── 8. Start data channel ─────────────────────────────────────────────
        {
            QEventLoop loop;
            bool dc_ok = false;
            QObject::connect(&sender, &CameraLatencySender::connected, &loop,
                [&]() { dc_ok = true; loop.quit(); });
            sender.start(data_token, data_url);
            QTimer::singleShot(20000, &loop, &QEventLoop::quit);
            loop.exec();
            if (!dc_ok) {
                qCritical() << "[Test] CameraLatencySender failed to connect within 20s.";
                goto cleanup;
            }
        }
        qDebug() << "[Test] Data channel connected. Collecting" << target << "samples...";

        // ── 9. Collect samples (hard 120s timeout) ────────────────────────────
        QTimer::singleShot(120000, &sample_loop, &QEventLoop::quit);
        sample_loop.exec();

        if (static_cast<int>(samples.size()) >= target) {
            exit_code = 0;
        } else {
            qCritical() << "[Test] Only" << samples.size() << "/" << target << "samples collected.";
        }
    }

    printStats(samples);

cleanup:
    sender.stop();
    camera.stop();
    director.disconnectFromRoom();
    {
        QEventLoop loop;
        QObject::connect(&client, &SessionClient::sessionClosed, &loop, &QEventLoop::quit);
        client.closeSession(room_code, "e2e-director");
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    livekit::shutdown();
    gst_deinit();
    return exit_code;
}
