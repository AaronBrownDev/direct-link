package janitor

import (
	"context"
	"errors"
	"log/slog"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/google/uuid"
	"github.com/redis/go-redis/v9"
)

const (
	// Redis key used to elect a single janitor leader. Runs the sweep
	janitorLockKey = "janitor:lock"
	sessionClosed  = "closed"
)

var releaseLockScript = redis.NewScript(`
	if redis.call("GET", KEYS[1]) == ARGV[1] then
		return redis.call("DEL", KEYS[1])
	end
	return 0
`)

// RoomDeleter is a callback that handles Livekit room and ingress cleanup
// for a given sessionID. It is called once per expired session during a sweep
type RoomDeleter func(ctx context.Context, sessionID string) error

// Janitor periodically sweeps for sessions whose TTL has elapsed and closes them,
// including LiveKit and ingress cleanup
type Janitor struct {
	store       session.Store    // used to fetch expired sessions and update their status
	redisClient *redis.Client    // used to acquire the distributed sweep lock
	deleteRoom  RoomDeleter      // callback  that handles LiveKit room and ingress cleanup
	logger      *slog.Logger     // structured logger
	interval    time.Duration    // how often the janitor sweeps
	lockTTL     time.Duration    // how long the distributed lock is held per sweep
	instanceID  string           // unique per-process ID used as the lock value
	metrics     *metrics.Metrics // Promethius metrics - may be nil, in which case metrics are skipped
}

// New creates a new Janitor
func New(
	store session.Store,
	redisClient *redis.Client,
	deleteRoom RoomDeleter,
	logger *slog.Logger,
	interval time.Duration,
	lockTTL time.Duration,
	m *metrics.Metrics) *Janitor {
	return &Janitor{
		store:       store,
		redisClient: redisClient,
		deleteRoom:  deleteRoom,
		logger:      logger,
		interval:    interval,
		lockTTL:     lockTTL,
		instanceID:  uuid.New().String(),
		metrics:     m,
	}
}

// Run starts the janitor ticker loop.
func (j *Janitor) Run(ctx context.Context) {
	j.logger.Info("janitor started", "interval", j.interval)

	// Sweep immediately on startup rather than waiting for the first tick.
	j.SweepAt(ctx, time.Now())

	ticker := time.NewTicker(j.interval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			j.SweepAt(ctx, time.Now())
		case <-ctx.Done():
			j.logger.Info("janitor stopped")
			return
		}

	}

}

func (j *Janitor) SweepAt(ctx context.Context, now time.Time) {
	result, err := j.redisClient.SetArgs(ctx, janitorLockKey, j.instanceID, redis.SetArgs{
		TTL:  j.lockTTL,
		Mode: "NX",
	}).Result()
	if err != nil && !errors.Is(err, redis.Nil) {
		j.logger.Error("janitor failed to acquire sweep lock", "error", err)
		return
	}
	// SetArgs with NX returns "OK" when the lock is acquired, redis.Nil when
	// the key already exists (another replica holds the lock).
	if result != "OK" {
		return
	}

	defer func() {
		//nolint:contextcheck // ctx may be cancelled on shutdown; a fresh context is required to release the lock
		releaseCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := releaseLockScript.Run(releaseCtx, j.redisClient, []string{janitorLockKey}, j.instanceID).Err(); err != nil && !errors.Is(err, redis.Nil) {
			j.logger.Warn("janitor failed to release sweep lock", "error", err)
		}
	}()

	expired, err := j.store.GetExpiredSessions(ctx, now)
	if err != nil {
		j.logger.Error("janitor failed to fetch expired sessions", "error", err)
		return
	}

	if len(expired) == 0 {
		return
	}
	j.logger.Info("janitor sweeping expired sessions", "count", len(expired))

	for _, sess := range expired {
		j.closeExpiredSession(ctx, sess)
	}
}

func (j *Janitor) closeExpiredSession(ctx context.Context, sess session.Session) {
	log := j.logger.With("session_id", sess.ID)

	if err := j.deleteRoom(ctx, sess.ID); err != nil {
		log.Warn("janitor LiveKit cleanup incomplete", "error", err)
	}

	if err := j.store.UpdateSessionStatus(ctx, sess.ID, sessionClosed); err != nil {
		log.Error("janitor failed to mark session closed", "error", err)
		return // metrics not incremented
	}

	if j.metrics != nil {
		j.metrics.SessionsExpiredTotal.Inc()
		j.metrics.SessionsActive.Dec()
	}
	log.Info("janitor closed expired session")

}
