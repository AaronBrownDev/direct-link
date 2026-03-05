package session_test

import (
	"context"
	"testing"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/alicebob/miniredis/v2"
)

// setTestStore
func newAccessTestStore(t *testing.T) *session.RedisStore {
	t.Helper()
	mr := miniredis.RunT(t)

	store, err := session.NewRedisStore(
		mr.Addr(), "", 0, 10, 2,
		time.Second, time.Second, time.Second,
		24*time.Hour,
	)
	if err != nil {
		t.Fatalf("failed to create store: %v", err)
	}
	return store
}

func newActiveSession(id, roomCode, owner string) *session.Session {
	return &session.Session{
		ID:         id,
		RoomCode:   roomCode,
		CreatedBy:  owner,
		CreatedAt:  time.Now(),
		MaxCameras: 4,
		Status:     "active",
	}
}

// Tests GrantAccess
func TestGrantAccess_UserHasAccess(t *testing.T) {
	store := newAccessTestStore(t)
	defer store.Close()
	ctx := context.Background()

	sess := newActiveSession("sess-1", "PROD-0001", "director-1")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}
	if err := store.GrantAccess(ctx, sess.ID, "cam-1", "camera"); err != nil {
		t.Fatalf("GrantAccess: %v", err)
	}

	has, err := store.HasAccess(ctx, sess.ID, "cam-1")
	if err != nil {
		t.Fatalf("HasAccess returned an error: %v", err)
	}
	if !has {
		t.Error("expected cam-1 to have access")
	}
}

// Tests GrantAccess for unknown user
func TestGrantAccess_UnknownUserHasNoAccess(t *testing.T) {
	store := newAccessTestStore(t)
	defer store.Close()
	ctx := context.Background()

	sess := newActiveSession("sess-2", "PROD-0002", "director-2")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	has, err := store.HasAccess(ctx, sess.ID, "nobody")
	if err != nil {
		t.Fatalf("HasAccess returned an error: %v", err)
	}
	if has {
		t.Error("expected nobody to NOT have access")
	}
}

// Tests Revoke access
// Removes users access
func TestRevokeAccess_RemoveUser(t *testing.T) {
	store := newAccessTestStore(t)
	defer store.Close()
	ctx := context.Background()

	sess := newActiveSession("sess-3", "PROD-0003", "director-3")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession returned an error: %v", err)
	}

	if err := store.GrantAccess(ctx, sess.ID, "cam-revoke", "camera"); err != nil {
		t.Fatalf("GrantAccess returned an error: %v", err)
	}
	if err := store.RevokeAccess(ctx, sess.ID, "cam-revoke"); err != nil {
		t.Fatalf("RevokeAccess: %v", err)
	}

	has, err := store.HasAccess(ctx, sess.ID, "cam-revoke")
	if err != nil {
		t.Fatalf("HasAccess: %v", err)
	}
	if has {
		t.Error("expected cam-revoke to have no access after revoke")
	}
}

// Test Revoke Access
// Ensures that other uses are not affected when one user's access is removed
func TestRevokeAccess_DoesNotAffectOtherUsers(t *testing.T) {
	store := newAccessTestStore(t)
	defer store.Close()

	ctx := context.Background()

	sess := newActiveSession("sess-4", "PROD-004", "director-4")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}
	if err := store.GrantAccess(ctx, sess.ID, "cam-a", "camera"); err != nil {
		t.Fatalf("GrantSession: %v", err)
	}

	if err := store.GrantAccess(ctx, sess.ID, "cam-b", "camera"); err != nil {
		t.Fatalf("GrantAccess: %v", err)
	}
	if err := store.RevokeAccess(ctx, sess.ID, "cam-a"); err != nil {
		t.Fatalf("RevokeAccess: %v", err)
	}

	has, err := store.HasAccess(ctx, sess.ID, "cam-b")
	if err != nil {
		t.Fatalf("HasAccess: %v", err)
	}
	if !has {
		t.Error("cam-b should still have access after revoking cam-a")
	}
}

// Tests GetRole
// Returns the CorrectRole of the user
func TestGetRole_ReturnsCorrectRole(t *testing.T) {
	store := newAccessTestStore(t)
	defer store.Close()
	ctx := context.Background()

	sess := newActiveSession("sess-5", "PROD-0005", "director-5")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}
	if err := store.GrantAccess(ctx, sess.ID, "cam-role", "camera"); err != nil {
		t.Fatalf("GrantAccess: %v", err)
	}

	role, err := store.GetRole(ctx, sess.ID, "cam-role")
	if err != nil {
		t.Fatalf("GetRole: %v", err)
	}
	if role != "camera" {
		t.Errorf("expected role 'camera', got %q", role)
	}
}

// Tests Get Role
// Returns empty because the user is empty
func TestGetRole_MissingUserReturnsEmpty(t *testing.T) {
	store := newAccessTestStore(t)
	defer store.Close()
	ctx := context.Background()

	sess := newActiveSession("sess-6", "PROD-0006", "director-6")
	if err := store.CreateSession(ctx, sess); err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	role, err := store.GetRole(ctx, sess.ID, "ghost")
	if err != nil {
		t.Fatalf("GetRole: %v", err)
	}
	if role != "" {
		t.Errorf("expected empty role for unknown user, got %q", role)
	}
}
