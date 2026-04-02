//go:build integration

package session_test

import (
	"context"
	"errors"
	"fmt"
	"os"
	"testing"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/redis/go-redis/v9"
)

func redisAddr() string {
	if addr := os.Getenv("REDIS_ADDR"); addr != "" {
		return addr
	}
	return "redis:6379"
}

func TestRedisConnection(t *testing.T) {
	client := redis.NewClient(&redis.Options{
		Addr: redisAddr(),
	})
	defer func() {
		if err := client.Close(); err != nil {
			t.Logf("failed to close client: %v", err)
		}
	}()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// Test connection with ping
	if err := client.Ping(ctx).Err(); err != nil {
		t.Fatalf("Failed to connect to Redis: %v", err)
	}

	// Test basic set/get
	key := "test:connection"
	value := "ok"

	if err := client.Set(ctx, key, value, time.Minute).Err(); err != nil {
		t.Fatalf("Failed to set key: %v", err)
	}

	result, err := client.Get(ctx, key).Result()
	if err != nil {
		t.Fatalf("Failed to get key: %v", err)
	}

	if result != value {
		t.Errorf("Expected %s, got %s", value, result)
	}

	// Cleanup
	client.Del(ctx, key)
}

func TestRedisStoreIntegration(t *testing.T) {
	store, err := session.NewRedisStore(session.RedisConfig{
		Addr:         redisAddr(),
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
		t.Fatalf("Failed to connect to Redis: %v", err)
	}

	defer func() {
		if err := store.Close(); err != nil {
			t.Logf("failed to close redis connection: %v", err)
		}
	}()

	ctx := context.Background()

	// Test 1:Ping (verifies Redis is reachable)
	if err := store.Ping(ctx); err != nil {
		t.Fatalf("Ping failed: %v", err)
	}

	// Test 2: Create and retrieve session
	type result struct {
		session *session.Session
		err     error
	}
	const n = 20

	results := make(chan result, n)

	for i := 0; i < n; i++ {
		go func(i int) {
			ts := time.Now().UnixNano()
			sess := &session.Session{
				ID:         fmt.Sprintf("concurrent-test-%d-%d", i, ts),
				RoomCode:   fmt.Sprintf("CON-TEST-%d-%d", i, ts),
				CreatedBy:  "test-director",
				CreatedAt:  time.Now(),
				MaxCameras: 10,
				Status:     "active",
			}
			err := store.CreateSession(ctx, sess)
			results <- result{sess, err}
		}(i)
	}
	var sessions []*session.Session
	for i := 0; i < n; i++ {
		r := <-results
		if r.err != nil {
			t.Errorf("CreateSession failed: %v", r.err)
			continue
		}
		sessions = append(sessions, r.session)
	}
	seen := make(map[string]struct{}, len(sessions))
	for _, sess := range sessions {
		if _, exists := seen[sess.RoomCode]; exists {
			t.Errorf("duplicate room code detected: %s", sess.RoomCode)
		}
		seen[sess.RoomCode] = struct{}{}
	}
	for idx, sess := range sessions {

		retrieved, err := store.GetSession(ctx, sess.ID)
		if err != nil {
			t.Fatalf("GetSession failed: %v", err)
		}
		if retrieved.RoomCode != sess.RoomCode {
			t.Errorf("expected RoomCode %s, got %s", sess.RoomCode, retrieved.RoomCode)
		}

		// Test 3 : Room code lookup
		byCode, err := store.GetSessionByRoomCode(ctx, sess.RoomCode)
		if err != nil {
			t.Fatalf("GetSessionByRoomCode failed %v", err)
		}

		if byCode.ID != sess.ID {
			t.Errorf("expected session ID %s, got %s", sess.ID, byCode.ID)
		}

		// Test 4: Access control
		if err := store.GrantAccess(ctx, sess.ID, "camera-test", "camera"); err != nil {
			t.Fatalf("GrantAccess failed: %v", err)
		}

		if len(sessions) > 1 {
			otherSess := sessions[(idx+1)%len(sessions)]
			crossAccess, err := store.HasAccess(ctx, otherSess.ID, "camera-test")
			if err != nil {
				t.Fatalf("HasAccess cross-session check failed: %v", err)
			}
			if crossAccess {
				t.Errorf("access leaked from %s into %s", sess.ID, otherSess.ID)
			}
		}

		hasAccess, err := store.HasAccess(ctx, sess.ID, "camera-test")
		if err != nil {
			t.Fatalf("HasAccess failed: %v", err)
		}

		if !hasAccess {
			t.Error("expected camera-test to have access")
		}

		// Test 5: Update session status
		if err := store.UpdateSessionStatus(ctx, sess.ID, "closed"); err != nil {
			t.Fatalf("UpdateSessionStatus failed: %v", err)
		}

		updated, err := store.GetSession(ctx, sess.ID)
		if err != nil {
			t.Fatalf("GetSession failed after update: %v", err)
		}

		if updated.Status != "closed" {
			t.Errorf("expected status 'closed', got '%s'", updated.Status)
		}

		// Test 6: Cleanup
		if err := store.DeleteSession(ctx, sess.ID); err != nil {
			t.Fatalf("DeleteSession failed: %v", err)
		}

		_, err = store.GetSession(ctx, sess.ID)
		if !errors.Is(err, session.ErrSessionNotFound) {
			t.Errorf("expected ErrSessionNotFound after deletion, got %v", err)
		}

		t.Log("\n All integration tests passed!")

	}
}
