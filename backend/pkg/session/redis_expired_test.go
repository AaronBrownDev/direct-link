package session_test

import (
	"context"
	"testing"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/alicebob/miniredis/v2"
)

const (
	// expiredTestSessionTTL is long enough that hash keys are never evicted
	// during the test. The sorted set score is written as now + expiredTestSessionTTL,
	// so expiredTestOffset must exceed it to make GetExpiredSessions return results.
	expiredTestSessionTTL = 7 * 24 * time.Hour
	expiredTestOffset     = expiredTestSessionTTL + time.Hour
)

// newExpiredSessionTestStore creates a store with a long TTL so that
// GetExpiredSessions can be tested by passing a future threshold without
// miniredis evicting the session hash keys.
func newExpiredSessionTestStore(t *testing.T, mr *miniredis.Miniredis) *session.RedisStore {
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

func newExpiredSession(id, roomCode, owner string) *session.Session {
	return &session.Session{
		ID:         id,
		RoomCode:   roomCode,
		CreatedBy:  owner,
		CreatedAt:  time.Now(),
		MaxCameras: 4,
		Status:     "active",
	}
}

// pastExpiry returns a time that is ahead of the sorted set score written at
// session creation (now + expiredTestSessionTTL), making GetExpiredSessions
// treat the session as expired without miniredis evicting the hash.
func pastExpiry() time.Time {
	return time.Now().Add(expiredTestOffset)
}

// TestGetExpiredSessions_NoExpiredSessions verifies that an empty slice is
// returned when no sessions have passed their TTL.
func TestGetExpiredSessions_NoExpiredSessions(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newExpiredSessionTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	sess := newExpiredSession("exp-sess-1", "ROOM-100001", "director-1")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	// Pass real time — score is now + 7 days so nothing is expired yet
	expired, err := store.GetExpiredSessions(ctx, time.Now())
	if err != nil {
		t.Fatalf("GetExpiredSessions: %v", err)
	}
	if len(expired) != 0 {
		t.Errorf("expected 0 expired sessions, got %d", len(expired))
	}
}

// TestGetExpiredSessions_ReturnsExpiredSession verifies that a session past
// its TTL is returned by GetExpiredSessions.
func TestGetExpiredSessions_ReturnsExpiredSession(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newExpiredSessionTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	sess := newExpiredSession("exp-sess-2", "ROOM-100002", "director-2")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	// pastExpiry() is ahead of the sorted set score — session appears expired.
	// The hash is still alive because the store TTL is 7 days.
	expired, err := store.GetExpiredSessions(ctx, pastExpiry())
	if err != nil {
		t.Fatalf("GetExpiredSessions: %v", err)
	}
	if len(expired) != 1 {
		t.Fatalf("expected 1 expired session, got %d", len(expired))
	}
	if expired[0].ID != sess.ID {
		t.Errorf("expected session ID %q, got %q", sess.ID, expired[0].ID)
	}
}

// TestGetExpiredSessions_SkipsClosedSessions verifies that sessions already
// marked closed are not returned, even if their score is in the past.
func TestGetExpiredSessions_SkipsClosedSessions(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newExpiredSessionTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	sess := newExpiredSession("exp-sess-3", "ROOM-100003", "director-3")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	if err := store.UpdateSessionStatus(ctx, sess.ID, "closed"); err != nil {
		t.Fatalf("UpdateSessionStatus: %v", err)
	}

	expired, err := store.GetExpiredSessions(ctx, pastExpiry())
	if err != nil {
		t.Fatalf("GetExpiredSessions: %v", err)
	}
	if len(expired) != 0 {
		t.Errorf("expected 0 expired sessions (already closed), got %d", len(expired))
	}
}

// TestGetExpiredSessions_ReturnsMultipleExpiredSessions verifies that all
// expired sessions are returned in a single call.
func TestGetExpiredSessions_ReturnsMultipleExpiredSessions(t *testing.T) {
	mr := miniredis.RunT(t)
	store := newExpiredSessionTestStore(t, mr)
	defer store.Close()

	ctx := context.Background()

	sessions := []*session.Session{
		newExpiredSession("exp-sess-4", "ROOM-100004", "director-4"),
		newExpiredSession("exp-sess-5", "ROOM-100005", "director-5"),
		newExpiredSession("exp-sess-6", "ROOM-100006", "director-6"),
	}

	for _, sess := range sessions {
		if err := store.CreateSession(ctx, sess); err != nil {
			t.Fatalf("CreateSession %s: %v", sess.ID, err)
		}
	}

	expired, err := store.GetExpiredSessions(ctx, pastExpiry())
	if err != nil {
		t.Fatalf("GetExpiredSessions: %v", err)
	}
	if len(expired) != len(sessions) {
		t.Errorf("expected %d expired sessions, got %d", len(sessions), len(expired))
	}
}
