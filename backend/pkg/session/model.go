package session

import "time"

// Session represents a room/Session for video streaming
type Session struct {
	ID         string    `json:"id`
	RoomCode   string    `json:"room_code"`
	CreatedBy  string    `json:"created_by"`
	CreatedAt  time.Time `json:"created_at"`
	MaxCameras int       `json:"max_cameras"`
	Status     string    `json:"status"` // "active" | "closed"
}

// Peer represents a participant in a session
type SessionAccess struct {
	SessionID string    `json:"session_id"`
	UserID    string    `json:"user_id"`
	Role      string    `json:"role"`
	GrantedAt time.Time `json:"granted_at"`
}
