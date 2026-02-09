package session

import "errors"

var (
	ErrSessionNotFound  = errors.New("Session not found")
	ErrPeerNotFound     = errors.New("peer not found")
	ErrSessionClosed    = errors.New("session is closed")
	ErrSessionFull      = errors.New("session has reached max peers")
	ErrInvalidRoomCode  = errors.New("invalid room code")
	ErrRedisUnavailable = errors.New("redis is unavailable")
)
