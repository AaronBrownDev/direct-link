/*
 * File: directortransport.hpp
 * Author: Justin Williams
 * Date: 3/15/26
 * File Description: Accesses LiveKit's RoomDelegate events to manage a LiveKit room
 * and its tracks. The QML application can connect to a LiveKit room given a LiveKit token
 * and url. The class owns a DirectorSession that it exposes to QML as a property. It can 
 * attach and detach LiveKit tracks from its DirectorSession.
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

#include "livekit/livekit.h"
#include "directorsession.hpp"

class DirectorTransport : public QObject, public livekit::RoomDelegate {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(DirectorSession* session READ session NOTIFY sessionChanged)

    public:
        explicit DirectorTransport(QObject *parent = nullptr);
        ~DirectorTransport() override;

        static DirectorTransport *create(QQmlEngine *engine, QJSEngine *) {
            return new DirectorTransport(engine);
        }

        void onParticipantConnected(
            livekit::Room &,
            const livekit::ParticipantConnectedEvent &event) override;

        void onTrackSubscribed(
            livekit::Room &,
            const livekit::TrackSubscribedEvent &event) override;

        void onTrackSubscriptionFailed(
            livekit::Room &,
            const livekit::TrackSubscriptionFailedEvent &event) override;

        void onTrackUnsubscribed(
            livekit::Room &,
            const livekit::TrackUnsubscribedEvent &event) override;

        void onConnectionStateChanged(
            livekit::Room &,
            const livekit::ConnectionStateChangedEvent &event) override;

        void onDisconnected(
            livekit::Room &,
            const livekit::DisconnectedEvent &event) override;

        [[nodiscard]] QString connectionState() const;
        [[nodiscard]] DirectorSession* session() const;

        Q_INVOKABLE void connectToRoom(const QString &token, const QString &url);
        Q_INVOKABLE void disconnectFromRoom();
        Q_INVOKABLE void shutdown();

    signals:
        void connected();
        void disconnected();
        void connectionStateChanged(const QString &newState);
        void sessionChanged();

    private:
        std::unique_ptr<livekit::Room> m_room;
        QString m_connection_state = "disconnected";
        std::unique_ptr<DirectorSession> m_session;
        QFutureWatcher<bool> *m_connectWatcher = nullptr;
};
