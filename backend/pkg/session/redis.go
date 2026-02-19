package session

import (
	"context"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/redis/go-redis/v9"
)

// Key prefixes following the technical design
const (
	sessionPrefix       = "session:"
	sessionAccessSuffix = ":access"
	roomCodePrefix      = "roomcode:"
	userSessionsPrefix  = "user:"
	activeSessionsKey   = "sessions:active"
	defaultSessionTTL   = 24 * time.Hour
)

// RedisStore implements the Store interface using redis
type RedisStore struct {
	client     *redis.Client
	sessionTTL time.Duration
}

// NewRedisStore creates a new Redis-backed store
func NewRedisStore(addr string,
	password string,
	db int,
	poolSize int,
	minIdleConns int,
	dialTimeout time.Duration,
	readTimeout time.Duration,
	writeTimeout time.Duration,
	sessionTTL time.Duration) (*RedisStore, error) {
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
		return nil, fmt.Errorf("%w: %v", ErrRedisUnavailable, err)
	}

	return &RedisStore{
		client:     client,
		sessionTTL: sessionTTL,
	}, nil

}

// CreateSession stores a new session in Redis
func (r *RedisStore) CreateSession(ctx context.Context, session *Session) error {
	// Use Redis Has to store session metadata
	sessionKey := sessionPrefix + session.ID

	// Convert session to map for HSET
	sessionData := map[string]interface{}{
		"id":          session.ID,
		"room_code":   session.RoomCode,
		"created_by":  session.CreatedBy,
		"created_at":  session.CreatedAt.Format(time.RFC3339),
		"max_cameras": session.MaxCameras,
		"status":      session.Status,
	}

	//Prune expired session IDs from active set
	r.client.ZRemRangeByScore(ctx, activeSessionsKey, "-inf",
		fmt.Sprintf("%d", time.Now().Unix()))

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

	//Add to user's sessions
	userSessionsKey := userSessionsPrefix + session.CreatedBy + ":sessions"
	pipe.SAdd(ctx, userSessionsKey, session.ID)
	pipe.Expire(ctx, userSessionsKey, r.sessionTTL)

	//3. Add to active sessions set
	pipe.ZAdd(ctx, activeSessionsKey, redis.Z{
		Score:  float64(time.Now().Add(r.sessionTTL).Unix()),
		Member: session.ID,
	})

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

	maxCameras, err := strconv.Atoi(result["max_cameras"])
	if err != nil {
		return nil, fmt.Errorf("failed to parse max_cameras: %w", err)
	}

	return &Session{
		ID:         result["id"],
		RoomCode:   result["room_code"],
		CreatedBy:  result["created_by"],
		CreatedAt:  createdAt,
		MaxCameras: maxCameras,
		Status:     result["status"],
	}, nil
}

func (r *RedisStore) GetSessionByRoomCode(ctx context.Context, code string) (*Session, error) {
	roomCodeKey := roomCodePrefix + code

	//Get session ID from room code
	sessionID, err := r.client.Get(ctx, roomCodeKey).Result()

	if err == redis.Nil {
		return nil, ErrInvalidRoomCode
	}
	if err != nil {
		return nil, err
	}

	// Get the actual session
	return r.GetSession(ctx, sessionID)
}
func (r *RedisStore) UpdateSessionStatus(ctx context.Context, sessionID string, status string) error {
	sessionKey := sessionPrefix + sessionID

	exists, err := r.client.Exists(ctx, sessionKey).Result()

	if err != nil {
		return err
	}
	if exists == 0 {
		return ErrSessionNotFound
	}
	if err := r.client.HSet(ctx, sessionKey, "status", status).Err(); err != nil {
		return err
	}

	// If closing, remove from active sessions
	if status == "closed" {
		r.client.ZRem(ctx, activeSessionsKey, sessionID)
	}
	return nil

}

func (r *RedisStore) DeleteSession(ctx context.Context, sessionID string) error {
	//Get session first to find room code
	session, err := r.GetSession(ctx, sessionID)

	if err != nil {
		return err
	}

	sessionKey := sessionPrefix + sessionID
	accessKey := sessionKey + sessionAccessSuffix
	userSessionsKey := userSessionsPrefix + session.CreatedBy + ":sessions"

	pipe := r.client.Pipeline()
	pipe.Del(ctx, sessionKey)
	pipe.Del(ctx, accessKey)
	pipe.SRem(ctx, userSessionsKey, sessionID)
	pipe.ZRem(ctx, activeSessionsKey, sessionID)

	if session.RoomCode != "" {
		pipe.Del(ctx, roomCodePrefix+session.RoomCode)
	}

	_, err = pipe.Exec(ctx)
	return err
}

// GrantAccess gives a user access to a session
func (r *RedisStore) GrantAccess(ctx context.Context, sessionID, userID, role string) error {
	sessionKey := sessionPrefix + sessionID
	accessKey := sessionKey + sessionAccessSuffix

	//Verify session exists
	exists, err := r.client.Exists(ctx, sessionKey).Result()
	if err != nil {
		return err
	}
	if exists == 0 {
		return ErrSessionNotFound
	}

	//Store as "userID:role" for easy lookup
	accessValue := fmt.Sprintf("%s:%s", userID, role)

	pipe := r.client.Pipeline()
	pipe.SAdd(ctx, accessKey, accessValue)
	pipe.Expire(ctx, accessKey, r.sessionTTL)

	_, err = pipe.Exec(ctx)
	return err
}

func (r *RedisStore) RevokeAccess(ctx context.Context, sessionID, userID string) error {
	sessionKey := sessionPrefix + sessionID
	accessKey := sessionKey + sessionAccessSuffix

	//Get all access entries to find the one for this user
	members, err := r.client.SMembers(ctx, accessKey).Result()

	if err != nil {
		return err
	}

	for _, member := range members {
		parts := strings.Split(member, ":")
		if len(parts) >= 1 && parts[0] == userID {
			return r.client.SRem(ctx, accessKey, member).Err()
		}
	}
	return nil
}

// HasAccess checks if a user has access to a session
func (r *RedisStore) HasAccess(ctx context.Context, sessionID, userID string) (bool, error) {
	sessionKey := sessionPrefix + sessionID
	accessKey := sessionKey + sessionAccessSuffix

	members, err := r.client.SMembers(ctx, accessKey).Result()

	if err != nil {
		return false, err
	}

	for _, member := range members {
		parts := strings.Split(member, ":")
		if len(parts) >= 1 && parts[0] == userID {
			return true, nil
		}
	}
	return false, nil
}

// GetUserSessions retrieves all sessions created by a user
func (r *RedisStore) GetUserSessions(ctx context.Context, userID string) ([]Session, error) {
	userSessionsKey := userSessionsPrefix + userID + ":sessions"

	sessionIDs, err := r.client.SMembers(ctx, userSessionsKey).Result()
	if err != nil {
		return nil, err
	}

	sessions := make([]Session, 0, len(sessionIDs))
	for _, sessionID := range sessionIDs {
		session, err := r.GetSession(ctx, sessionID)
		if err != nil {
			//Only skip if session expired/deleted (expected)
			if errors.Is(err, ErrSessionNotFound) {
				continue
			}
			//For any other error (Redis down, parse error, etc.), fail fast
			return nil, fmt.Errorf("failed to get session %s: %w", sessionID, err)
		}
		sessions = append(sessions, *session)
	}
	return sessions, nil
}

// Ping checks Redis availabilty
func (r *RedisStore) Ping(ctx context.Context) error {
	if err := r.client.Ping(ctx).Err(); err != nil {
		return fmt.Errorf("%w: %v", ErrRedisUnavailable, err)
	}
	return nil
}

// Close closes the redis connection
func (r *RedisStore) Close() error {
	return r.client.Close()
}
