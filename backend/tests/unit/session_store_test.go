package unit

import (
	"context"
	"testing"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/alicebob/miniredis/v2"
)

func setupTestStore(t *testing.T) (session.Store, *miniredis.Miniredis) {
	mr := miniredis.RunT(t)

	store, err := session.NewRedisStore(
		mr.Addr(),
		"",
		0,
		10,
		2,
		time.Second,
		time.Second,
		time.Second,
		24*time.Hour,
	)
	if err != nil {
		t.Fatalf("failed to create store: %v", err)
	}
	return store, mr
}

func TestCreateAndGetSession(t *testing.T) {
	store, _ := setupTestStore(t)
	defer func() {
		if err := store.Close(); err != nil {
			t.Logf("failed to close connection: %v", err)
		}
	}()

	ctx := context.Background()

	session := &session.Session{
		ID:         "test_session_1",
		RoomCode:   "DEMO_001",
		CreatedBy:  "director-123",
		CreatedAt:  time.Now(),
		MaxCameras: 10,
		Status:     "active",
	}

	//Test Create
	if err := store.CreateSession(ctx, session); err != nil {
		t.Fatalf("CreateSession failed: %v", err)
	}

	//Test Get by ID
	retrieved, err := store.GetSession(ctx, session.ID)
	if err != nil {
		t.Fatalf("GetSession failed: %v", err)
	}

	if retrieved.RoomCode != session.RoomCode {
		t.Errorf("expected RoomCode %s, got %s", session.RoomCode, retrieved.RoomCode)
	}
}

func TestGetSessionByRoomCode(t *testing.T) {
	store, _ := setupTestStore(t)
	defer func() {
		if err := store.Close(); err != nil {
			t.Logf("failed to close connection: %v", err)
		}
	}()

	ctx := context.Background()

	sess := &session.Session{
		ID:         "test-session-2",
		RoomCode:   "CODE-123",
		CreatedBy:  "director-456",
		CreatedAt:  time.Now(),
		MaxCameras: 10,
		Status:     "active",
	}

	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession failed: %v", err)
	}

	//Test Get by room code
	retrieved, err := store.GetSessionByRoomCode(ctx, sess.RoomCode)
	if err != nil {
		t.Fatalf("GetSessionByRoomCode failed : %v", err)
	}

	if retrieved.ID != sess.ID {
		t.Errorf("expected ID %s, got %s", sess.ID, retrieved.ID)
	}

}

func TestGrantAndCheckAccess(t *testing.T) {
	store, _ := setupTestStore(t)
	defer func() {
		if err := store.Close(); err != nil {
			t.Logf("failed to close connection: %v", err)
		}
	}()

	ctx := context.Background()

	sess := &session.Session{
		ID:         "test-session-3",
		RoomCode:   "ACCESS-001",
		CreatedBy:  "director-789",
		CreatedAt:  time.Now(),
		MaxCameras: 10,
		Status:     "active",
	}

	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession failed: %v", err)
	}

	//Grant access
	if err := store.GrantAccess(ctx, sess.ID, "camera-123", "camera"); err != nil {
		t.Fatalf("GrantAccess failed %v", err)
	}

	//Check Access
	hasAccess, err := store.HasAccess(ctx, sess.ID, "camera-123")
	if err != nil {
		t.Fatalf("HasAccess failed: %v", err)
	}

	if !hasAccess {
		t.Error("expected user to have access")
	}

	// Check non-existent user
	hasAccess, err = store.HasAccess(ctx, sess.ID, "camera-999")
	if err != nil {
		t.Fatalf("HasAccess failed: %v", err)
	}

	if hasAccess {
		t.Error("expected user to NOT have access")
	}
}

func TestUpdateSessionStatus(t *testing.T) {
	store, _ := setupTestStore(t)
	defer func() {
		if err := store.Close(); err != nil {
			t.Logf("failed to close connection: %v", err)
		}
	}()

	ctx := context.Background()

	sess := &session.Session{
		ID:         "test-session-4",
		RoomCode:   "STATUS-001",
		CreatedBy:  "director-111",
		CreatedAt:  time.Now(),
		MaxCameras: 10,
		Status:     "active",
	}
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession failed: %v", err)
	}

	//Update Status
	if err := store.UpdateSessionStatus(ctx, sess.ID, "closed"); err != nil {
		t.Fatalf("UpdateSessionStatus failed %v", err)
	}

	retrieved, err := store.GetSession(ctx, sess.ID)
	if err != nil {
		t.Fatalf("GetSession failed %v", err)
	}

	if retrieved.Status != "closed" {
		t.Errorf("expected status 'closed', got '%s'", retrieved.Status)
	}

}
func TestDeleteSession(t *testing.T) {
	store, _ := setupTestStore(t)
	defer func() {
		if err := store.Close(); err != nil {
			t.Logf("failed to close connection: %v", err)
		}
	}()

	ctx := context.Background()

	sess := &session.Session{
		ID:         "test-session-delete",
		RoomCode:   "DELETE-001",
		CreatedBy:  "director-222",
		CreatedAt:  time.Now(),
		MaxCameras: 10,
		Status:     "active",
	}

	//Create Session
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession failed: %v", err)
	}

	//Delete Session
	if err := store.DeleteSession(ctx, sess.ID); err != nil {
		t.Fatalf("DeleteSession failed: %v", err)
	}

	//Verify it's gone
	_, err := store.GetSession(ctx, sess.ID)
	if err != session.ErrSessionNotFound {
		t.Errorf("expected ErrSessionNotFound after deletion, got %v", err)
	}
}

func TestPing(t *testing.T) {
	store, mr := setupTestStore(t)
	defer func() {
		if err := store.Close(); err != nil {
			t.Logf("failed to close connection: %v", err)
		}
	}()

	ctx := context.Background()

	//Should succeed
	if err := store.Ping(ctx); err != nil {
		t.Errorf("Ping failed: %v", err)
	}

	//Close miniredis to simulate failure
	mr.Close()

	// Should fail
	if err := store.Ping(ctx); err == nil {
		t.Error("expected Ping to fail after Redis shutdown")
	}
}
