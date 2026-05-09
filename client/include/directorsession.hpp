/*
 * File: directorsession.hpp
 * Author: Justin Williams
 * Date: 3/27/26
 * File Description: A class that owns a FrameReader and accepts LiveKit tracks from
 * DirectorTransport. Attached tracks are stored using a map of VideoTrack objects. The
 * QML application can access a list of the attached VideoTrack objects.
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QtConcurrent/QtConcurrent>
#include <QList>
#include <map>

#include "videotrack.hpp"

class DirectorSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("DirectorSession is managed by DirectorTransport.")

    Q_PROPERTY(QList<QObject*> tracks READ tracks NOTIFY tracksChanged)

    public:
        explicit DirectorSession(QObject *parent = nullptr);
        ~DirectorSession() override;

        DirectorSession(const DirectorSession &) = delete;
        DirectorSession &operator=(const DirectorSession &) = delete;
        DirectorSession(DirectorSession &&) = delete;
        DirectorSession &operator=(DirectorSession &&) = delete;

        [[nodiscard]] QList<QObject*> tracks() const;

        void attachTrack(const std::shared_ptr<livekit::Track> &track,
                         const std::string &trackSid,
                         const QString &participantIdentity);
        void detachTrack(const std::string &trackSid);
        void detachAllTracks();

    signals:
        void tracksChanged();
        void trackAdded(qsizetype index);
        void trackRemoved(qsizetype index);
        // Aggregates frameReceived from all attached VideoTracks.
        //   receivedSteadyNs     — local steady_clock ns from VideoTrack::readLoop.
        //   frameTimestampUs     — VideoFrameEvent::timestamp_us (sender-domain
        //                          capture-time estimate, microseconds).
        //   participantIdentity  — identity of the publishing participant; used
        //                          by DirectorTransport to filter the latency
        //                          matcher down to a single camera.
        void frameArrived(qint64 receivedSteadyNs, qint64 frameTimestampUs,
                          const QString &participantIdentity);
        // Forwarded from each VideoTrack on its stats-poll tick.  Carries
        // participantIdentity so DirectorTransport can filter to the active
        // main-preview camera.
        void videoStats(double jitterBufferMs, double decodeMs,
                        double networkJitterMs, double framesPerSecond,
                        const QString &participantIdentity);
        // Forwarded from the first VideoTrack that reports a valid resolution.
        void videoResolutionChanged(int width, int height);

    private:
        std::map<std::string, std::unique_ptr<VideoTrack>> m_trackMap;
};
