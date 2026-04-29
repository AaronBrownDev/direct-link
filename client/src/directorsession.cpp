#include <iterator>

#include "directorsession.hpp"

DirectorSession::DirectorSession(QObject *parent) : QObject(parent) { };

DirectorSession::~DirectorSession() {
    detachAllTracks();
}

QList<QObject*> DirectorSession::tracks() const {
    QList<QObject*> track_list;

    for (const auto &[sid, track] : m_trackMap) {
        track_list.append(track.get());
    }

    return track_list;
}

void DirectorSession::attachTrack(const std::shared_ptr<livekit::Track> &track, const std::string &trackSid) {
    if (m_trackMap.find(trackSid) != m_trackMap.end()) {
        qWarning() << "[DirectorSession] Track already attached.\n\tsid=" << trackSid;
        return;
    }
    
    auto v_track = std::make_unique<VideoTrack>(this);
    if (!v_track->setTrack(track)) {
        qWarning() << "[DirectorSession] Failed to attach track.\n\tsid=" << trackSid;
        v_track.reset();
        return;
    }

    connect(v_track.get(), &VideoTrack::frameReceived, this, &DirectorSession::frameArrived);
    m_trackMap[trackSid] = std::move(v_track);
    auto inserted = m_trackMap.find(trackSid);
    auto index = static_cast<qsizetype>(std::distance(m_trackMap.begin(), inserted));
    emit trackAdded(index);
    emit tracksChanged();
}

void DirectorSession::detachTrack(const std::string &trackSid) {
    auto it = m_trackMap.find(trackSid);
    if (it == m_trackMap.end()) { return; }
    
    auto index = static_cast<qsizetype>(std::distance(m_trackMap.begin(), it));
    m_trackMap.erase(it);
    emit trackRemoved(index);
    emit tracksChanged();
}

void DirectorSession::detachAllTracks() {
    m_trackMap.clear();
    emit tracksChanged();
}
