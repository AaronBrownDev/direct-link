package integration

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/redis/go-redis/v9"
)

func TestRedisConnection(t *testing.T) {
	client := redis.NewClient(&redis.Options{
		Addr: "localhost:6379",
	})
	defer client.Close()

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
	store, err := session.NewRedisStore(
		"localhost:6379",
		"",
		0,
		10,
		2,
		5*time.Second,
		3*time.Second,
		3*time.Second,
		24*time.Hour)

	if err != nil {
		t.Fatalf("Failed to connect to Redis: %v", err)
	}

	defer store.Close()

	ctx := context.Background()

	//Test 1:Ping (verifies Redis is reachable)
	if err := store.Ping(ctx); err != nil {
		t.Fatalf("Ping failed: %v", err)
	}

	//Test 2: Create and retrieve session
	sessionID := fmt.Sprintf("integration-test-%d", time.Now().UnixNano())

	sess := &session.Session{
		ID:         sessionID,
		RoomCode:   "INT-TEST",
		CreatedBy:  "test-director",
		CreatedAt:  time.Now(),
		MaxCameras: 10,
		Status:     "active",
	}

	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("Create Session failed: %v", err)
	}
	defer store.DeleteSession(ctx, sessionID)

	retrieved, err := store.GetSession(ctx, sessionID)
	if err != nil {
		t.Fatalf("GetSession failed: %v", err)
	}
	if retrieved.RoomCode != sess.RoomCode {
		t.Errorf("expected RoomCode %s, got %s", sess.RoomCode, retrieved.RoomCode)
	}

	//Test 3 : Room code lookup
	byCode, err := store.GetSessionByRoomCode(ctx, "INT-TEST")
	if err != nil {
		t.Fatalf("GetSessionByRoomCode failed %v", err)
	}

	if byCode.ID != sessionID {
		t.Errorf("expected session ID %s, got %s", sessionID, byCode.ID)
	}

	//Test 4: Access control
	if err := store.GrantAccess(ctx, sessionID, "camera-test", "camera"); err != nil {
		t.Fatalf("GrantAccess failed: %v", err)
	}

	hasAccess, err := store.HasAccess(ctx, sessionID, "camera-test")
	if err != nil {
		t.Fatalf("HasAccess failed: %v", err)
	}

	if !hasAccess {
		t.Error("expected camera-test to have access")
	}

	//Test 5: Update session status
	if err := store.UpdateSessionStatus(ctx, sessionID, "closed"); err != nil {
		t.Fatalf("UpdateSessionStatus failed: %v", err)
	}

	updated, err := store.GetSession(ctx, sessionID)
	if err != nil {
		t.Fatalf("GetSession failed after update: %v", err)
	}

	if updated.Status != "closed" {
		t.Errorf("expected status 'closed', got '%s'", updated.Status)
	}

	// Test 6: Cleanup
	if err := store.DeleteSession(ctx, sessionID); err != nil {
		t.Fatalf("DeleteSession failed: %v", err)
	}

	_, err = store.GetSession(ctx, sessionID)
	if err != session.ErrSessionNotFound {
		t.Errorf("expected ErrSessionNotFound after deletion, got %v", err)
	}

	t.Log("\n All integration tests passed!")

}
