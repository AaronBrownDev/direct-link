package session

import (
	"context"
	"fmt"
	"time"

	"github.com/redis/go-redis/v9"
)

// Key prefixes following the technical design
const (
	sessionPrefix      = "session:"
	sessionPeersPrefix = "session:%s:peers" //Format with session ID
	peerPrefix         = "peer:"
	roomCodePrefix     = "roomcode:"
	activeSessionsKey  = "session:active"
)

// RedisStore implements the Store interface using redis
type RedisStore struct {
	client     *redis.Client
	sessionTTL time.Duration
	peerTTL    time.Duration
}

// NewReidStore creates a new Redis-backed store
func NewRedisStore(addr, password string, db, poolSize, minIdleConns int,
	dialTimeout, readTimeout, writeTimeout time.Duration,
	sessionTTL, peerTTL time.Duration) (*RedisStore, error) {
	client := redis.NewClient(&redis.Options{
		Addr:         addr,
		Password:     password,
		DB:           db,
		PoolSize:     poolSize,
		MinIdleConns: minIdleConns,
		DialTimeout:  dialTimeout,
		ReadTimeout:  readTimeout,
		WriteTimeout: writeTimeout,
	})

	//Test connection
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := client.Ping(ctx).Err(); err != nil {
		return nil, err
	}

	return &RedisStore{
		client:     client,
		sessionTTL: sessionTTL,
		peerTTL:    peerTTL,
	}, nil

}

// CreateSession stores a new session in Redis
func (r *RedisStore) CreateSession(ctx context.Context, session *Session) error {
	// Use Redis Has to store session metadata
	sessionKey := sessionPrefix + session.ID

	// Convert session to map for HSET
	sessionData := map[string]interface{}{
		"id":         session.ID,
		"room_code":  session.RoomCode,
		"created_at": session.CreatedAt.Format(time.RFC3339),
		"max_peers":  session.MaxPeers,
		"status":     session.Status,
	}

	// Use pipeline for atomic operations
	pipe := r.client.Pipeline()

	//1. Store session hash
	pipe.HSet(ctx, sessionKey, sessionData)
	pipe.Expire(ctx, sessionKey, r.sessionTTL)

	// 2. Create room code mapping
	if session.RoomCode != "" {
		roomCodeKey := roomCodePrefix + session.RoomCode
		pipe.Set(ctx, roomCodeKey, session.ID, r.sessionTTL)
	}

	//3. Add to active sessions set
	pipe.SAdd(ctx, activeSessionsKey, session.ID)

	//Execute all commands atomically
	if _, err := pipe.Exec(ctx); err != nil {
		return err
	}

	return nil

}

// GetSession retrieves a session by ID
func (r *RedisStore) GetSession(ctx context.Context, sessionID string) (*Session, error) {
	sessionKey := sessionPrefix + sessionID

	result, err := r.client.HGetAll(ctx, sessionKey).Result()

	if err != nil {
		return nil, err
	}

	if len(result) == 0 {
		return nil, ErrSessionNotFound
	}

	//Parse created_at timestamp
	createdAt, err := time.Parse(time.RFC3339, result["created_at"])

	if err != nil {
		return nil, fmt.Errorf("failed to parse created_at %w", err)
	}

	var maxPeers int
	fmt.Scanf(result["max_peers"], "%d", &maxPeers)

	session := &Session{
		ID:        result["id"],
		RoomCode:  result["room_code"],
		CreatedAt: createdAt,
		MaxPeers:  maxPeers,
		Status:    result["status"],
	}

	return session, nil
}

func (r *RedisStore) GetSessionByRoomCode(ctx context.Context, code string) (*Session, error) {
	roomCodeKey := roomCodePrefix + code

	//Get session ID from room code
	sessionID, err := r.client.Get(ctx, roomCodeKey).Result()

	if err == redis.Nil {
		return nil, ErrInvalidRoomCode
	}
	if err != nil {
		return nil, fmt.Errorf("failed to lookup room code: %w", err)
	}

	// Get the actual session
	return r.GetSession(ctx, sessionID)
}

func (r *RedisStore) DeleteSession(ctx context.Context, sessionID string) error {
	//Get session first to find room code
	session, err := r.GetSession(ctx, sessionID)

	if err != nil {
		return err
	}

	sessionKey := sessionPrefix + sessionID
	peersKey := fmt.Sprintf(sessionPeersPrefix, sessionID)

	//Get all peers to delete them too
	peerIDs, err := r.client.SMembers(ctx, peersKey).Result()
	if err != nil && err != redis.Nil {
		return fmt.Errorf("failed to get peers for deletion: %w", err)
	}

	//Use pipeline for atomic election
	pipe := r.client.Pipeline()

	//Delete session
	pipe.Del(ctx, sessionKey)
	pipe.Del(ctx, peersKey)

	//Delete room code mapping
	if session.RoomCode != "" {
		pipe.Del(ctx, roomCodePrefix+session.RoomCode)
	}

	//Remove from active sessions
	pipe.SRem(ctx, activeSessionsKey, sessionID)

	//Delete all peers
	for _, peerID := range peerIDs {
		pipe.Del(ctx, peerPrefix+peerID)
	}

	_, err = pipe.Exec(ctx)
	return err
}

func (r *RedisStore) AddPeer(ctx context.Context, peer *Peer) error {
	//Check is session exists and is active
	session, err := r.GetSession(ctx, peer.SessionID)
	if err != nil {
		return err
	}

	if session.Status == "closed" {
		return ErrSessionClosed
	}

	//Check if session is full
	peerCount, err := r.client.SCard(ctx, fmt.Sprintf(sessionPeersPrefix, peer.SessionID)).Result()
	if err != nil && err != redis.Nil {
		return fmt.Errorf("failed to check peer count: %w", err)
	}
	if int(peerCount) >= session.MaxPeers {
		return ErrSessionFull
	}

	peerKey := peerPrefix + peer.ID
	peersKey := fmt.Sprintf(sessionPeersPrefix, peer.SessionID)

	//Store peer data as hash
	peerData := map[string]interface{}{
		"id":         peer.ID,
		"session_id": peer.SessionID,
		"user_id":    peer.UserID,
		"role":       peer.Role,
		"joined_at":  peer.JoinedAt.Format(time.RFC3339),
		"status":     peer.Status,
	}

	//Use pipeline for atomic operations
	pipe := r.client.Pipeline()

	//Store peer hash
	pipe.HSet(ctx, peerKey, peerData)
	pipe.Expire(ctx, peerKey, r.peerTTL)

	//Add to session's peer set
	pipe.SAdd(ctx, peersKey, peer.ID)
	pipe.Expire(ctx, peersKey, r.sessionTTL)

	_, err = pipe.Exec(ctx)
	if err != nil {
		return fmt.Errorf("failed to add peer: %w", err)
	}

	return nil
}

func (r *RedisStore) RemovePeer(ctx context.Context, peerID string) error {
	peerKey := peerPrefix + peerID

	//Get peer to find sessionID
	peerData, err := r.client.HGetAll(ctx, peerKey).Result()
	if err != nil {
		return fmt.Errorf("failed to get peer: %w", err)
	}

	if len(peerData) == 0 {
		return ErrPeerNotFound
	}

	sessionID := peerData["session_id"]
	peersKey := fmt.Sprintf(sessionPeersPrefix, sessionID)

	//Use pipeline for atomic removal
	pipe := r.client.Pipeline()
	pipe.Del(ctx, peerKey)
	pipe.SRem(ctx, peersKey, peerID)

	_, err = pipe.Exec(ctx)

	return err
}

func (r *RedisStore) GetSessionPeers(ctx context.Context, sessionID string) ([]Peer, error) {
	peersKey := fmt.Sprintf(sessionPeersPrefix, sessionID)

	//Get All peer IDs
	peerIDs, err := r.client.SMembers(ctx, peersKey).Result()
	if err != nil {
		return nil, fmt.Errorf("failed to get peer IDs: %w", err)
	}
	peers := make([]Peer, 0, len(peerIDs))

	//Get each peer's data
	for _, peerID := range peerIDs {
		peerKey := peerPrefix + peerID
		peerData, err := r.client.HGetAll(ctx, peerKey).Result()
		if err != nil || len(peerData) == 0 {
			continue //Skip id peer data is missing
		}

		joinedAt, _ := time.Parse(time.RFC3339, peerData["joined_at"])

		peer := Peer{
			ID:        peerData["id"],
			SessionID: peerData["session_id"],
			UserID:    peerData["user_id"],
			Role:      peerData["role"],
			JoinedAt:  joinedAt,
			Status:    peerData["status"],
		}

		peers = append(peers, peer)
	}

	return peers, nil
}

func (r *RedisStore) UpdatePeerStatus(ctx context.Context, peerID string, status string) error {
	peerKey := peerPrefix + peerID

	// Check if peer exists
	exists, err := r.client.Exists(ctx, peerKey).Result()
	if err != nil {
		return fmt.Errorf("failed to check peer existence: %w", err)
	}

	if exists == 0 {
		return ErrPeerNotFound
	}

	// Update status field
	if err := r.client.HSet(ctx, peerKey, "status", status).Err(); err != nil {
		return fmt.Errorf("failed to update peer status: %w", err)
	}

	// Refresh TTL
	r.client.Expire(ctx, peerKey, r.peerTTL)

	return nil
}

func (r *RedisStore) Ping(ctx context.Context) error {
	if err := r.client.Ping(ctx).Err(); err != nil {
		return fmt.Errorf("%w: %v", ErrRedisUnavailable, err)
	}
	return nil
}

func (r *RedisStore) Close() error {
	return r.client.Close()
}
