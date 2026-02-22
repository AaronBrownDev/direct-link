package integration

import (
	"context"
	"log/slog"
	"os"
	"testing"
	"time"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/AaronBrownDev/direct-link/internal/signaling"
)

// Creates new test Server
func newTestServer(t *testing.T) *signaling.Server {
	t.Helper()
	logger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError}))
	cfg := signaling.Config{
		RedisAddr:         "redis:6379",
		RedisPassword:     "",
		RedisDB:           0,
		RedisPoolSize:     10,
		RedisMinIdle:      2,
		RedisDialTimeout:  5 * time.Second,
		RedisReadTimeout:  3 * time.Second,
		RedisWriteTimeout: 3 * time.Second,
		SessionTTL:        24 * time.Hour,
		LiveKitHost:       "http://livekit:7880",
		//TODO: Check if this is hardcoded or called in the another
		LiveKitAPIKey:    "devkey",
		LiveKitAPISecret: "secret",
	}
	return signaling.NewServer(cfg, logger)
}

// Tests CreateSession
func TestCreateSession_Success(t *testing.T) {
	srv := newTestServer(t)
	ctx := context.Background()

	resp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
		UserId:     "director-1",
		MaxCameras: 4,
	})

	if err != nil {
		t.Fatalf("CreateSession returned err: %v", err)
	}

	if resp.SessionId == "" {
		t.Error("expected non-empty session_id")
	}

	if resp.RoomCode == "" {
		t.Error("expected non-empty room_code")
	}
}

// Tests CreateSession with missing UserID
func TestCreateSession_MissingUserID(t *testing.T) {
	srv := newTestServer(t)
	ctx := context.Background()

	_, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{})

	if err == nil {
		t.Fatal("expected error for missing user_id")
	}
}

// Tests close session when owner closes
func TestCloseSession_OwnerCanClose(t *testing.T) {
	srv := newTestServer(t)
	ctx := context.Background()

	createResp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
		UserId: "owner-1"})

	if err != nil {
		t.Fatalf("CreateSession: %v", err)
	}

	closeResp, err := srv.CloseSession(ctx, &pb.CloseSessionRequest{
		SessionId: createResp.SessionId,
		UserId:    "owner-1",
	})

	if err != nil {
		t.Fatalf("CloseSession: %v", err)
	}

	if !closeResp.Success {
		t.Errorf("expected success=true")
	}
}

// Tests Close Session when a none owner tries to close the Session
func TestCloseSession_NonOwnerRejected(t *testing.T) {
	srv := newTestServer(t)
	ctx := context.Background()

	createResp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
		UserId: "owner-2"})

	if err != nil {
		t.Fatalf("CreateSession gave an error: %v", err)
	}

	_, err = srv.CloseSession(ctx, &pb.CloseSessionRequest{
		SessionId: createResp.SessionId,
		UserId:    "intruder",
	})

	if err == nil {
		t.Fatal("expected error for non-owner")
	}
}

// Tests close session when the session doesn't exists
func TestCloseSession_SessionNotFound(t *testing.T) {
	srv := newTestServer(t)
	ctx := context.Background()

	_, err := srv.CloseSession(ctx, &pb.CloseSessionRequest{
		SessionId: "nonexistent",
		UserId:    "anyone",
	})

	if err == nil {
		t.Fatal("expected error for nonexistent session")
	}
}

// Tests GetMySessions
func TestGetMySessions_ReturnSessions(t *testing.T) {
	srv := newTestServer(t)
	ctx := context.Background()

	//Use a unique user ID per test run to avoid interference from other test
	userID := "director-list-" + time.Now().Format("20060102150405")

	srv.CreateSession(ctx, &pb.CreateSessionRequest{UserId: userID})
	srv.CreateSession(ctx, &pb.CreateSessionRequest{UserId: userID})

	resp, err := srv.GetMySessions(ctx, &pb.GetMySessionRequest{UserId: userID})
	if err != nil {
		t.Fatalf("GetMySessions: %v", err)
	}
	if len(resp.Sessions) != 2 {
		t.Errorf("expected 2 sessions, got %d", len(resp.Sessions))
	}
}

// Tests GetMySessions when user ID is not given
func TestGetMySessions_MissingUserID(t *testing.T) {
	srv := newTestServer(t)
	ctx := context.Background()

	_, err := srv.GetMySessions(ctx, &pb.GetMySessionRequest{})
	if err == nil {
		t.Fatal("expected error for missing user_id")
	}
}
