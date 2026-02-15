package session

import "context"

// Store defines the interface for session storage operations
type Store interface {
	//Session operations
	CreateSession(ctx context.Context, session *Session) error
	GetSession(ctx context.Context, sessionID string) (*Session, error)
	GetSessionByRoomCode(ctx context.Context, code string) (*Session, error)
	UpdateSessionStatus(ctx context.Context, sessionID string, status string) error
	DeleteSession(ctx context.Context, sessionID string) error

	//Access control
	GrantAccess(ctx context.Context, sessionID, userID, role string) error
	RevokeAccess(ctx context.Context, sessionID, userID string) error
	HasAccess(ctx context.Context, sessionID, userID string) (bool, error)

	//User Sessions
	GetUserSessions(ctx context.Context, userID string) ([]Session, error)

	//Health check
	Ping(ctx context.Context) error

	// Cleanup
	Close() error
}
