package session

import "time"

// Session represents a room/Session for video streaming
type Session struct {
	ID        string    `json:"id`
	RoomCode  string    `json:"room_code"`
	CreatedAt time.Time `json:"created_at"`
	MaxPeers  int       `json:"max_peers"`
	Status    string    `json:"status"` // "active" | "closed"
}

// Peer represents a participant in a session
type Peer struct {
	ID        string    `json:"id"`
	SessionID string    `json:"session_id"`
	UserID    string    `json:"user_id"`
	Role      string    `json:"role"` // "camera" | "director"
	JoinedAt  time.Time `json:"joined_at"`
	Status    string    `json:"status"` // "connected" | "disconnected"
}
