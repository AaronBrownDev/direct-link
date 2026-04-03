package janitor_test

import (
	"context"
	"errors"
	"log/slog"
	"os"
	"testing"
	"time"

	"github.com/AaronBrownDev/direct-link/internal/janitor"
	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/alicebob/miniredis/v2"
	"github.com/redis/go-redis/v9"
)

const (
	// testSessionTTL is the TTL used when creating the test store.
	// It must be long enough that FastForward does not evict session hashes.
	testSessionTTL = 7 * 24 * time.Hour

	// testExpiredOffset is how far ahead expiredNow() sits relative to real
	// time. It must exceed testSessionTTL so the sorted set scores (written as
	// now + testSessionTTL) fall before the SweepAt threshold.
	testExpiredOffset = testSessionTTL + time.Hour

	sessionIsClosed = "closed"
)

type getExpiredSessionsFailure struct {
	session.Store
}

func (s *getExpiredSessionsFailure) GetExpiredSessions(_ context.Context, _ time.Time) ([]session.Session, error) {
	return nil, errors.New("simulated GetExpiredSessions failure")
}

// --- Helpers ---

// newTestStore creates a store with a 7-day session TTL. This keeps the
// session hash alive well past expiredNow() while still letting the sorted
// set scores fall in the past when SweepAt is called with expiredNow().
func newTestStore(t *testing.T, mr *miniredis.Miniredis) *session.RedisStore {
	t.Helper()
	store, err := session.NewRedisStore(session.RedisConfig{
		Addr:         mr.Addr(),
		Password:     "",
		Db:           0,
		PoolSize:     10,
		MinIdleConns: 2,
		DialTimeout:  time.Second,
		ReadTimeout:  time.Second,
		WriteTimeout: time.Second,
		SessionTTL:   24 * time.Hour,
		MaxRetries:   3,
		RetryBackoff: 100 * time.Millisecond,
	})
	if err != nil {
		t.Fatalf("failed to create store: %v", err)
	}
	return store
}
func newLockClient(mr *miniredis.Miniredis) *redis.Client {
	return redis.NewClient(&redis.Options{
		Addr: mr.Addr(),
	})
}

func newTestJanitor(store session.Store, redisClient *redis.Client, deleteRoom janitor.RoomDeleter) *janitor.Janitor {
	logger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError}))
	return janitor.New(store, redisClient, deleteRoom, logger, time.Minute, time.Minute, metrics.New())
}

func newSession(id, roomCode, owner string) *session.Session {
	return &session.Session{
		ID:         id,
		RoomCode:   roomCode,
		CreatedBy:  owner,
		CreatedAt:  time.Now(),
		MaxCameras: 4,
		Status:     "active",
	}
}

// expiredNow returns a time ahead of the sorted set score written at session
// creation (now + testSessionTTL). Passing this to SweepAt makes the janitor
// treat the session as expired without miniredis evicting the hash keys.
func expiredNow() time.Time {
	return time.Now().Add(testExpiredOffset)
}

// --- Tests ---

// TestJanitor_NoExpiredSessions verifies that SweepAt is a no-op when there
// are no sessions past their TTL.
func TestJanitor_NoExpiredSessions(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	sess := newSession("sess-1", "ROOM-000001", "director-1")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	deleteRoomCalled := false
	j := newTestJanitor(store, newLockClient(mr), func(_ context.Context, _ string) error {
		deleteRoomCalled = true
		return nil
	})

	// Pass real time — score is now + 7 days, so nothing is expired yet
	j.SweepAt(ctx, time.Now())

	if deleteRoomCalled {
		t.Error("expected deleteRoom not to be called when no sessions are expired")
	}

	retrieved, err := store.GetSession(ctx, sess.ID)
	if err != nil {
		t.Fatalf("GetSession: %v", err)
	}
	if retrieved.Status != "active" {
		t.Errorf("expected session to remain active, got %q", retrieved.Status)
	}
}

// TestJanitor_ClosesExpiredSession verifies that an expired session is marked
// closed and deleteRoom is called with the correct session ID.
func TestJanitor_ClosesExpiredSession(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newTestStore(t, mr)

	defer store.Close()

	ctx := context.Background()

	sess := newSession("sess-2", "ROOM-000002", "director-2")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	var deletedSessionID string
	j := newTestJanitor(store, newLockClient(mr), func(_ context.Context, sessionID string) error {
		deletedSessionID = sessionID
		return nil
	})

	// expiredNow() is past the sorted set score (now + 7 days) so the janitor
	// treats this session as expired. The hash is still alive (not evicted).
	j.SweepAt(ctx, expiredNow())

	if deletedSessionID != sess.ID {
		t.Errorf("expected deleteRoom called with %q, got %q", sess.ID, deletedSessionID)
	}

	retrieved, err := store.GetSession(ctx, sess.ID)
	if err != nil {
		t.Fatalf("GetSession: %v", err)
	}
	if retrieved.Status != sessionIsClosed {
		t.Errorf("expected session status 'closed', got %q", retrieved.Status)
	}
}

// TestJanitor_NoIngressIDs verifies that a session with no ingress IDs is
// still closed cleanly — the deleteRoom callback must still be called.
func TestJanitor_NoIngressIDs(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	sess := newSession("sess-3", "ROOM-000003", "director-3")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	deleteRoomCalled := false
	j := newTestJanitor(store, newLockClient(mr), func(_ context.Context, _ string) error {
		deleteRoomCalled = true
		return nil
	})

	j.SweepAt(ctx, expiredNow())

	if !deleteRoomCalled {
		t.Error("expected deleteRoom to be called even with no ingress IDs")
	}

	retrieved, err := store.GetSession(ctx, sess.ID)
	if err != nil {
		t.Fatalf("GetSession: %v", err)
	}
	if retrieved.Status != sessionIsClosed {
		t.Errorf("expected session status 'closed', got %q", retrieved.Status)
	}
}

// TestJanitor_RedisError verifies that a Redis failure during GetExpiredSessions
// does not panic and SweepAt returns gracefully.
func TestJanitor_RedisError(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	failStore := &getExpiredSessionsFailure{Store: store}

	deleteRoomCalled := false
	j := newTestJanitor(failStore, newLockClient(mr), func(_ context.Context, _ string) error {
		deleteRoomCalled = true
		return nil
	})

	// Shut down miniredis to simulate a Redis infrastructure failure
	mr.Close()

	// Should not panic
	j.SweepAt(ctx, expiredNow())

	if deleteRoomCalled {
		t.Error("expected deleteRoom not to be called when Redis is unavailable")
	}
}

// TestJanitor_MultipleExpiredSessions verifies that all expired sessions are
// cleaned up in a single sweep.
func TestJanitor_MultipleExpiredSessions(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	sessions := []*session.Session{
		newSession("sess-5", "ROOM-000005", "director-5"),
		newSession("sess-6", "ROOM-000006", "director-6"),
		newSession("sess-7", "ROOM-000007", "director-7"),
	}

	for _, sess := range sessions {
		if err := store.CreateSession(ctx, sess); err != nil {
			t.Fatalf("CreateSession %s: %v", sess.ID, err)
		}
	}

	var deletedIDs []string
	j := newTestJanitor(store, newLockClient(mr), func(_ context.Context, sessionID string) error {
		deletedIDs = append(deletedIDs, sessionID)
		return nil
	})

	j.SweepAt(ctx, expiredNow())

	if len(deletedIDs) != len(sessions) {
		t.Errorf("expected %d deleteRoom calls, got %d", len(sessions), len(deletedIDs))
	}

	for _, sess := range sessions {
		retrieved, err := store.GetSession(ctx, sess.ID)
		if err != nil {
			t.Fatalf("GetSession %s: %v", sess.ID, err)
		}
		if retrieved.Status != sessionIsClosed {
			t.Errorf("session %s: expected status 'closed', got %q", sess.ID, retrieved.Status)
		}
	}
}
