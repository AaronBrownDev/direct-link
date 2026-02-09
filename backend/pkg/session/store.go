package session

import "context"

// Store defines the interface for session storage operations
type Store interface {
	//Session operations
	CreateSession(ctx context.Context, session *Session) error
	GetSession(ctx context.Context, sessionID string) (*Session, error)
	GetSessionByRoomCode(ctx context.Context, code string) (*Session, error)

	//Peer operations
	AddPeer(ctx context.Context, peer *Peer) error
	RemovePeer(ctx context.Context, peerID string) error
	GetSessionPeers(ctx context.Context, sessionId string) ([]Peer, error)
	UpdatePeerStatus(ctx context.Context, peerID string, status string) error

	//Health check
	Ping(ctx context.Context) error

	// Cleanup
	Close() error
}
