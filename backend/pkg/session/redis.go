package session

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"strconv"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/redis/go-redis/v9"
)

// Key prefixes following the technical design
const (
	sessionPrefix        = "session:"
	sessionAccessSuffix  = ":access"
	sessionIngressSuffix = ":ingress_ids"
	roomCodePrefix       = "roomcode:"
	userSessionsPrefix   = "user:"
	activeSessionsKey    = "sessions:active"
	defaultSessionTTL    = 24 * time.Hour

	// Prevents silent mismatches from bare comparisons
	statusClosed = "closed"
)

type RedisConfig struct {
	Addr         string
	Password     string
	Db           int
	PoolSize     int
	MinIdleConns int
	DialTimeout  time.Duration
	ReadTimeout  time.Duration
	WriteTimeout time.Duration
	SessionTTL   time.Duration
	MaxRetries   int
	RetryBackoff time.Duration
}

// RedisStore implements the Store interface using redis
type RedisStore struct {
	client       *redis.Client
	sessionTTL   time.Duration
	metrics      *metrics.Metrics
	maxRetries   int
	retryBackOff time.Duration
}

// NewRedisStore creates a new Redis-backed store
func NewRedisStore(cfg RedisConfig) (*RedisStore, error) {
	if cfg.RetryBackoff <= 0 {
		return nil, fmt.Errorf("session: RetryBackoff must be positive, got %v", cfg.RetryBackoff)
	}

	client := redis.NewClient(&redis.Options{
		Addr:         cfg.Addr,
		Password:     cfg.Password,
		DB:           cfg.Db,
		PoolSize:     cfg.PoolSize,
		MinIdleConns: cfg.MinIdleConns,
		DialTimeout:  cfg.DialTimeout,
		ReadTimeout:  cfg.ReadTimeout,
		WriteTimeout: cfg.WriteTimeout,
	})

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := client.Ping(ctx).Err(); err != nil {
		return nil, fmt.Errorf("%w: %w", ErrRedisUnavailable, err)
	}

	return &RedisStore{
		client:       client,
		sessionTTL:   cfg.SessionTTL,
		maxRetries:   cfg.MaxRetries,
		retryBackOff: cfg.RetryBackoff,
	}, nil
}

// CreateSession stores a new session in Redis
func (r *RedisStore) CreateSession(ctx context.Context, session *Session) (err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("create_session", err, start)
	}()

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

	userSessionsKey := userSessionsPrefix + session.CreatedBy + ":sessions"

	err = r.retryRedisOp(ctx, func() error {
		pipe := r.client.Pipeline()

		// 1. Store session hash
		pipe.HSet(ctx, sessionKey, sessionData)
		pipe.Expire(ctx, sessionKey, r.sessionTTL)

		// 2. Create room code mapping
		if session.RoomCode != "" {
			roomCodeKey := roomCodePrefix + session.RoomCode
			pipe.Set(ctx, roomCodeKey, session.ID, r.sessionTTL)
		}

		// 3. Add to user's sessions
		pipe.SAdd(ctx, userSessionsKey, session.ID)
		pipe.Expire(ctx, userSessionsKey, r.sessionTTL)

		// 4. Add to active sessions set
		pipe.ZAdd(ctx, activeSessionsKey, redis.Z{
			Score:  float64(time.Now().Add(r.sessionTTL).Unix()),
			Member: session.ID,
		})

		_, e := pipe.Exec(ctx)
		return e
	})

	return err

}

// GetSession retrieves a session by ID
func (r *RedisStore) GetSession(ctx context.Context, sessionID string) (session *Session, err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("get_session", err, start)
	}()

	sessionKey := sessionPrefix + sessionID

	var result map[string]string
	err = r.retryRedisOp(ctx, func() error {
		var e error
		result, e = r.client.HGetAll(ctx, sessionKey).Result()
		return e
	})

	if err != nil {
		return nil, err
	}

	if len(result) == 0 {
		return nil, ErrSessionNotFound
	}

	// Parse created_at timestamp
	createdAt, err := time.Parse(time.RFC3339, result["created_at"])

	if err != nil {
		return nil, fmt.Errorf("failed to parse created_at %w", err)
	}

	maxCameras, err := strconv.ParseInt(result["max_cameras"], 10, 32)
	if err != nil {
		return nil, fmt.Errorf("failed to parse max_cameras: %w", err)
	}

	return &Session{
		ID:         result["id"],
		RoomCode:   result["room_code"],
		CreatedBy:  result["created_by"],
		CreatedAt:  createdAt,
		MaxCameras: int32(maxCameras),
		Status:     result["status"],
	}, nil
}

func (r *RedisStore) GetSessionByRoomCode(ctx context.Context, code string) (session *Session, err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("get_session_by_room_code", err, start)
	}()

	roomCodeKey := roomCodePrefix + code
	var sessionID string
	err = r.retryRedisOp(ctx, func() error {
		var e error
		// Get session ID from room code
		sessionID, e = r.client.Get(ctx, roomCodeKey).Result()
		if errors.Is(e, redis.Nil) {
			return ErrInvalidRoomCode
		}
		return e
	})
	if err != nil {
		return nil, err
	}

	// Get the actual session
	session, err = r.GetSession(ctx, sessionID)

	return session, err
}

func (r *RedisStore) UpdateSessionStatus(ctx context.Context, sessionID string, status string) (err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("update_session_status", err, start)
	}()

	sessionKey := sessionPrefix + sessionID

	var exists int64
	err = r.retryRedisOp(ctx, func() error {
		var e error
		exists, e = r.client.Exists(ctx, sessionKey).Result()
		return e
	})
	if err != nil {
		return err
	}
	if exists == 0 {
		return ErrSessionNotFound
	}
	err = r.retryRedisOp(ctx, func() error {
		return r.client.HSet(ctx, sessionKey, "status", status).Err()
	})
	if err != nil {
		return err
	}

	// If closing, remove from active sessions — warn only since the janitor will reconcile on next sweep
	if status == statusClosed {
		if zErr := r.retryRedisOp(ctx, func() error {
			return r.client.ZRem(ctx, activeSessionsKey, sessionID).Err()
		}); zErr != nil {
			slog.Warn("failed to remove session from active set", "session_id", sessionID, "error", zErr)
		}
	}

	return nil

}

func (r *RedisStore) DeleteSession(ctx context.Context, sessionID string) (err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("delete_session", err, start)
	}()

	// Get session first to find room code
	session, err := r.GetSession(ctx, sessionID)

	if err != nil {
		return err
	}

	sessionKey := sessionPrefix + sessionID
	accessKey := sessionKey + sessionAccessSuffix
	userSessionsKey := userSessionsPrefix + session.CreatedBy + ":sessions"

	err = r.retryRedisOp(ctx, func() error {
		pipe := r.client.Pipeline()
		pipe.Del(ctx, sessionKey)
		pipe.Del(ctx, accessKey)
		pipe.SRem(ctx, userSessionsKey, sessionID)
		pipe.ZRem(ctx, activeSessionsKey, sessionID)

		if session.RoomCode != "" {
			pipe.Del(ctx, roomCodePrefix+session.RoomCode)
		}

		_, e := pipe.Exec(ctx)
		return e
	})

	return err
}

// GetExpiredSessions returns sessions that have expired based on the active sessions sorted set.
func (r *RedisStore) GetExpiredSessions(ctx context.Context, now time.Time) (sessions []Session, err error) {
	start := time.Now()
	defer func() {
		r.observeRedisOp("get_expired_sessions", err, start)
	}()

	// Get expired session IDs from the sorted set
	stop := fmt.Sprintf("%d", now.Unix())
	var sessionIDs []string
	err = r.retryRedisOp(ctx, func() error {
		var e error
		sessionIDs, e = r.client.ZRangeArgs(ctx, redis.ZRangeArgs{
			Key:     activeSessionsKey,
			Start:   "-inf",
			Stop:    stop,
			ByScore: true,
		}).Result()
		return e
	})
	if err != nil {
		return nil, err
	}

	sessions = make([]Session, 0, len(sessionIDs))
	for _, sessionID := range sessionIDs {
		session, err := r.GetSession(ctx, sessionID)
		if err != nil {
			// If session not found, it may have already been cleaned up. Just skip it.
			if errors.Is(err, ErrSessionNotFound) {
				continue
			}
			return nil, fmt.Errorf("failed to get expired session %s: %w", sessionID, err)
		}
		if session.Status == statusClosed {
			if zErr := r.client.ZRem(ctx, activeSessionsKey, sessionID).Err(); zErr != nil {
				slog.Warn("failed to remove expired session", "session_id", session.ID, "error", zErr)
			}
			continue
		}
		sessions = append(sessions, *session)
	}
	return sessions, nil

}

// GetRole returns the role assigned to a user in a session, or an empty
// string if the user has no entry
func (r *RedisStore) GetRole(ctx context.Context, sessionID, userID string) (str string, err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("get_role", err, start)
	}()

	accessKey := sessionPrefix + sessionID + sessionAccessSuffix

	var role string
	err = r.retryRedisOp(ctx, func() error {
		var e error
		role, e = r.client.HGet(ctx, accessKey, userID).Result()
		if errors.Is(e, redis.Nil) {
			return nil
		}
		return e
	})
	if err != nil {
		return "", err
	}
	return role, nil
}

// GrantAccess gives a user access to a session
func (r *RedisStore) GrantAccess(ctx context.Context, sessionID, userID, role string) (err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("grant_access", err, start)
	}()

	sessionKey := sessionPrefix + sessionID
	accessKey := sessionKey + sessionAccessSuffix

	// Verify session exists
	var exists int64
	err = r.retryRedisOp(ctx, func() error {
		var e error
		exists, e = r.client.Exists(ctx, sessionKey).Result()
		return e
	})
	if err != nil {
		return err
	}
	if exists == 0 {
		return ErrSessionNotFound
	}

	err = r.retryRedisOp(ctx, func() error {
		pipe := r.client.Pipeline()
		pipe.HSet(ctx, accessKey, userID, role)
		pipe.Expire(ctx, accessKey, r.sessionTTL)
		_, e := pipe.Exec(ctx)
		return e
	})
	return err
}

// RevokeAccess removes a user's access entry atomically
func (r *RedisStore) RevokeAccess(ctx context.Context, sessionID, userID string) (err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("revoke_access", err, start)
	}()

	accessKey := sessionPrefix + sessionID + sessionAccessSuffix

	err = r.retryRedisOp(ctx, func() error {
		return r.client.HDel(ctx, accessKey, userID).Err()
	})
	return err
}

// HasAccess checks if a user has access to a session
func (r *RedisStore) HasAccess(ctx context.Context, sessionID, userID string) (hasAccess bool, err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("has_access", err, start)
	}()

	accessKey := sessionPrefix + sessionID + sessionAccessSuffix

	var exists bool
	err = r.retryRedisOp(ctx, func() error {
		var e error
		exists, e = r.client.HExists(ctx, accessKey, userID).Result()
		return e
	})
	return exists, err
}

// GetUserSessions retrieves all sessions created by a user
func (r *RedisStore) GetUserSessions(ctx context.Context, userID string) (sessions []Session, err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("get_user_sessions", err, start)
	}()

	userSessionsKey := userSessionsPrefix + userID + ":sessions"

	var sessionIDs []string
	err = r.retryRedisOp(ctx, func() error {
		var e error
		sessionIDs, e = r.client.SMembers(ctx, userSessionsKey).Result()
		return e
	})
	if err != nil {
		return nil, err
	}

	sessions = make([]Session, 0, len(sessionIDs))
	for _, sessionID := range sessionIDs {
		session, err := r.GetSession(ctx, sessionID)
		if err != nil {
			// Only skip if session expired/deleted (expected)
			if errors.Is(err, ErrSessionNotFound) {
				// Delete old sessions reference from the user's set
				r.client.SRem(ctx, userSessionsKey, sessionID)
				continue
			}
			// For any other error (Redis down, parse error, etc.), fail fast
			return nil, fmt.Errorf("failed to get session %s: %w", sessionID, err)
		}
		sessions = append(sessions, *session)
	}
	return sessions, nil
}

// AddIngressID stores an ingress ID against a session for later cleanup.
func (r *RedisStore) AddIngressID(ctx context.Context, sessionID, ingressID string) (err error) {
	start := time.Now()
	defer func() {
		r.observeRedisOp("add_ingress_id", err, start)
	}()

	key := sessionPrefix + sessionID + sessionIngressSuffix
	err = r.retryRedisOp(ctx, func() error {
		pipe := r.client.Pipeline()
		pipe.SAdd(ctx, key, ingressID)
		pipe.Expire(ctx, key, r.sessionTTL)
		_, e := pipe.Exec(ctx)
		return e
	})
	return err
}

// GetIngressIDs returns all ingress IDs associated with a given session.
func (r *RedisStore) GetIngressIDs(ctx context.Context, sessionID string) (ids []string, err error) {
	start := time.Now()
	defer func() {
		r.observeRedisOp("get_ingress_ids", err, start)
	}()

	key := sessionPrefix + sessionID + sessionIngressSuffix
	err = r.retryRedisOp(ctx, func() error {
		var e error
		ids, e = r.client.SMembers(ctx, key).Result()
		return e
	})
	return ids, err
}

// Ping checks Redis availability
func (r *RedisStore) Ping(ctx context.Context) (err error) {

	start := time.Now()
	defer func() {
		r.observeRedisOp("ping", err, start)
	}()

	if err = r.client.Ping(ctx).Err(); err != nil {
		return fmt.Errorf("%w: %w", ErrRedisUnavailable, err)
	}
	return nil
}

// Close closes the redis connection
func (r *RedisStore) Close() error {
	return r.client.Close()
}

// SetMetrics enables Prometheus instrumentation for Redis operations.
// If not called, redis operations work without metrics.
// TODO: Refactor to a metrics-aware wrapper that implements the Store interface.
func (r *RedisStore) SetMetrics(m *metrics.Metrics) {
	r.metrics = m
}

// observeRedisOp records the duration and any error for a Redis operation.
// This centralizes instrumentation so individual Store methods stay focused
// on their Redis logic.
func (r *RedisStore) observeRedisOp(operation string, err error, start time.Time) {

	if r.metrics != nil {
		// TODO: Consider adding a "status" label (success/error) to the histogram to separate latency distributions for successful vs failed operations.
		duration := time.Since(start).Seconds()
		r.metrics.RedisOperationDuration.WithLabelValues(operation).Observe(duration)
		if isRedisErr(err) {
			r.metrics.RedisErrorsTotal.WithLabelValues(operation).Inc()
		}
	}
}

// isRedisErr returns true if the error represents a Redis infrastructure
// failure rather than an application-level condition like "not found".
func isRedisErr(err error) bool {
	if err == nil {
		return false
	}
	if errors.Is(err, redis.Nil) {
		return false
	}
	return !errors.Is(err, ErrSessionNotFound) &&
		!errors.Is(err, ErrInvalidRoomCode) &&
		!errors.Is(err, ErrSessionClosed)
}

// returns true for transient infrastructure errors worth retrying
// It excludes application-level sentinels, context cancellation and deadline expiry
func isRetryable(err error) bool {
	if err == nil {
		return false
	}
	if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
		return false
	}
	if errors.Is(err, redis.Nil) {
		return false
	}
	if errors.Is(err, ErrSessionNotFound) ||
		errors.Is(err, ErrInvalidRoomCode) ||
		errors.Is(err, ErrSessionClosed) {
		return false
	}
	return true
}

func (r *RedisStore) retryRedisOp(ctx context.Context, op func() error) error {
	maxAttempts := r.maxRetries
	if maxAttempts < 1 {
		maxAttempts = 1
	}
	var err error
	for attempt := range maxAttempts {
		err = op()
		if err == nil || !isRetryable(err) {
			return err
		}
		if attempt < maxAttempts-1 {
			backoff := r.retryBackOff * (1 << attempt)
			slog.Warn("redis operation failed, retrying", "attempt", attempt+1, "max", r.maxRetries, "backoff", backoff, "error", err)
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(backoff):
			}
		}
	}
	return err
}
