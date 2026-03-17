/*
 * File: directortransport.hpp
 * Author: Justin Williams
 * Date: 3/14/26
 * File Description: 
 */

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>

#include "livekit/livekit.h"
#include "directorsession.hpp"

class DirectorTransport : public QObject, public livekit::RoomDelegate {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(DirectorSession* session READ session NOTIFY sessionChanged)

    public:
        static DirectorTransport *instance() {
            static DirectorTransport instance;
            return &instance;
        }

        ~DirectorTransport() override;

        static DirectorTransport *create(QQmlEngine *, QJSEngine *) {
            return instance();
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

    public slots:
        void connectToRoom(const QString &token, const QString &url);
        void disconnectFromRoom();

    signals:
        void connected();
        void disconnected();
        void connectionStateChanged(const QString &newState);
        void sessionChanged();
        

    private:
        explicit DirectorTransport(QObject *parent = nullptr);

        std::unique_ptr<livekit::Room> m_room;
        QString m_connection_state = "disconnected";
        std::unique_ptr<DirectorSession> m_session;
};
