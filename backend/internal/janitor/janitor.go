package janitor

import (
	"context"
	"log/slog"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/AaronBrownDev/direct-link/pkg/session"
)

// RoomDeleter is a callback that handles Livekit room and ingress cleanup
// for a given sessionID. It is called once per expired session during a sweep
type RoomDeleter func(ctx context.Context, sessionID string)

// Janitor periodically sweeps for sessions whose TTL has elapsed and closes them,
// inclding LiveKit and ingress cleanup
type Janitor struct {
	store      session.Store    // used to fetch expired sessions and update their status
	deleteRoom RoomDeleter      // callback  that handles LiveKit room and ingress cleanup
	logger     *slog.Logger     // structured logger
	interval   time.Duration    // how often the janitor sweeps
	metrics    *metrics.Metrics // Promethius metrics - may be nil, in which case metrics are skipped
}

// New creates a new Janitor
func New(
	store session.Store,
	deleteRoom RoomDeleter,
	logger *slog.Logger,
	interval time.Duration,
	m *metrics.Metrics) *Janitor {
	return &Janitor{
		store:      store,
		deleteRoom: deleteRoom,
		logger:     logger,
		interval:   interval,
		metrics:    m,
	}
}

// Run starts the janitor ticket loop.
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

	j.deleteRoom(ctx, sess.ID)

	if err := j.store.UpdateSessionStatus(ctx, sess.ID, "closed"); err != nil {
		log.Error("janitor failed to mark session closed", "error", err)
	}

	if j.metrics != nil {
		j.metrics.SessionsExpiredTotal.Inc()
		j.metrics.SessionsActive.Dec()
	}
	log.Info("janitor closed expired session")

}
