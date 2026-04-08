//go:build integration

package signaling_test

import (
	"context"
	"log/slog"
	"os"
	"testing"
	"time"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/AaronBrownDev/direct-link/internal/signaling"
)

func redisAddr() string {
	if addr := os.Getenv("REDIS_ADDR"); addr != "" {
		return addr
	}
	return "redis:6379"
}

// Creates new test Server
func newTestServer(t *testing.T) *signaling.Server {
	t.Helper()
	redisAddr := redisAddr()

	logger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError}))
	cfg := signaling.DefaultConfig()
	cfg.RedisAddr = redisAddr
	cfg.LiveKitHost = "http://livekit:7880"
	cfg.LiveKitAPIKey = "devkey"
	cfg.LiveKitAPISecret = "secret"
	return signaling.NewServer(cfg, logger)
}

func TestServer_CreateSession(t *testing.T) {
	t.Run("session created successfully", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		resp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
			UserId:     "director-1",
			MaxCameras: 4,
		})

		if err != nil {
			t.Fatalf("CreateSession returned err: %v", err)
		}

		if resp.RoomCode == "" {
			t.Error("expected non-empty room_code")
		}
	})

	t.Run("missing user ID", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		_, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{})

		if err == nil {
			t.Fatal("expected error for missing user_id")
		}
	})
}

// Tests close session when owner closes
func TestServer_CloseSession(t *testing.T) {

	t.Run("only owner can close a session", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		createResp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
			UserId:     "owner-1",
			MaxCameras: 4,
		})

		if err != nil {
			t.Fatalf("CreateSession: %v", err)
		}

		_, err = srv.CloseSession(ctx, &pb.CloseSessionRequest{
			RoomCode: createResp.RoomCode,
			UserId:   "owner-1",
		})

		if err != nil {
			t.Fatalf("CloseSession: %v", err)
		}
	})

	t.Run("nonowner rejected from closing the branch", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		createResp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
			UserId:     "owner-2",
			MaxCameras: 4,
		})

		if err != nil {
			t.Fatalf("CreateSession: %v", err)
		}

		_, err = srv.CloseSession(ctx, &pb.CloseSessionRequest{
			RoomCode: createResp.RoomCode,
			UserId:   "intruder",
		})

		if err == nil {
			t.Fatal("expected error for non-owner")
		}
	})

	t.Run("session not found", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		_, err := srv.CloseSession(ctx, &pb.CloseSessionRequest{
			RoomCode: "ROOM-0000",
			UserId:   "anyone",
		})

		if err == nil {
			t.Fatal("expected error for nonexistent session")
		}
	})

}

// Tests GetMySessions
func TestServer_GetMySessions(t *testing.T) {
	t.Run("user's sessions returned", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		// Use a unique user ID per test run to avoid interference from other test
		userID := "director-list-" + time.Now().Format("20060102150405")

		_, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
			UserId:     userID,
			MaxCameras: 4,
		})
		if err != nil {
			t.Fatalf("CreateSession: %v", err)
		}
		_, err = srv.CreateSession(ctx, &pb.CreateSessionRequest{
			UserId:     userID,
			MaxCameras: 4,
		})
		if err != nil {
			t.Fatalf("CreateSession: %v", err)
		}

		resp, err := srv.GetMySessions(ctx, &pb.GetMySessionsRequest{UserId: userID})
		if err != nil {
			t.Fatalf("GetMySessions: %v", err)
		}
		if len(resp.Sessions) != 2 {
			t.Errorf("expected 2 sessions, got %d", len(resp.Sessions))
		}
	})

	t.Run("missing user's ID", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		_, err := srv.GetMySessions(ctx, &pb.GetMySessionsRequest{})

		if err == nil {
			t.Fatal("expected error for missing user_id")
		}
	})

}

// TestJoinSession_ByRoomCode tests if the user is able to join with just a room code
func TestServer_JoinSession(t *testing.T) {
	t.Run("join session successful", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		createResp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
			UserId:     "director-join",
			MaxCameras: 4,
		})
		if err != nil {
			t.Fatalf("CreateSession: %v", err)
		}
		_, err = srv.JoinSession(ctx, &pb.JoinRequest{
			RoomCode: createResp.RoomCode,
			UserId:   "director-join",
			Role:     "director",
		})

		if err != nil {
			t.Fatalf("JoinSession: %v", err)
		}
	})

	t.Run("user not allowed to close session", func(t *testing.T) {
		srv := newTestServer(t)
		ctx := context.Background()

		createResp, err := srv.CreateSession(ctx, &pb.CreateSessionRequest{
			UserId:     "director-close",
			MaxCameras: 4,
		})
		if err != nil {
			t.Fatalf("CreateSession: %v", err)
		}
		_, err = srv.CloseSession(ctx, &pb.CloseSessionRequest{
			RoomCode: createResp.RoomCode,
			UserId:   "director-close",
		})
		if err != nil {
			t.Fatalf("CloseSession: %v", err)
		}

		_, err = srv.JoinSession(ctx, &pb.JoinRequest{
			RoomCode: createResp.RoomCode,
			UserId:   "cam-late",
			Role:     "camera",
		})

		if err == nil {
			t.Fatal("expected error joining closed session")
		}
	})

}
