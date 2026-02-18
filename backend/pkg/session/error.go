package session

import "errors"

var (
	ErrSessionNotFound  = errors.New("Session not found")
	ErrSessionClosed    = errors.New("session is closed")
	ErrSessionFull      = errors.New("session has reached max cameras")
	ErrInvalidRoomCode  = errors.New("invalid room code")
	ErrRedisUnavailable = errors.New("redis is unavailable")
	ErrAccessDenied     = errors.New("access denied")
)
