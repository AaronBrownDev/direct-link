package signaling

import (
	"context"
	"errors"
	"testing"
	"time"

	"log/slog"
	"os"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/alicebob/miniredis/v2"
	"github.com/livekit/protocol/livekit"
)

// --- Mock ingress client ---

type mockIngressClient struct {
	createFunc func(*livekit.CreateIngressRequest) (*livekit.IngressInfo, error)
	deleteFunc func(*livekit.DeleteIngressRequest) (*livekit.IngressInfo, error)
	deletedIDs []string
}

func (m *mockIngressClient) CreateIngress(_ context.Context, req *livekit.CreateIngressRequest) (*livekit.IngressInfo, error) {
	return m.createFunc(req)
}

func (m *mockIngressClient) DeleteIngress(_ context.Context, req *livekit.DeleteIngressRequest) (*livekit.IngressInfo, error) {
	m.deletedIDs = append(m.deletedIDs, req.IngressId)
	if m.deleteFunc != nil {
		return m.deleteFunc(req)
	}
	return &livekit.IngressInfo{IngressId: req.IngressId}, nil
}

// --- Helpers ---

func newUnitTestServer(t *testing.T, mock *mockIngressClient) *Server {
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

	return &Server{
		cfg:             Config{LiveKitHost: "http://localhost:7880", LiveKitExternalURL: "ws://localhost:7880", LiveKitAPIKey: "devkey", LiveKitAPISecret: "secret"},
		store:           store,
		logger:          slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError})),
		lkIngressClient: mock,
		metrics:         metrics.New(),
	}
}

func seedSession(t *testing.T, store session.Store) *session.Session {
	t.Helper()
	sess := &session.Session{
		ID: "test-session-id", RoomCode: "ROOM-TEST", CreatedBy: "director-1",
		CreatedAt: time.Now().UTC(), MaxCameras: 4, Status: "active",
	}
	if err := store.CreateSession(context.Background(), sess); err != nil {
		t.Fatalf("seedSession: %v", err)
	}
	return sess
}

func defaultMockIngress() *mockIngressClient {
	return &mockIngressClient{
		createFunc: func(_ *livekit.CreateIngressRequest) (*livekit.IngressInfo, error) {
			return &livekit.IngressInfo{
				IngressId: "ingress-abc",
				Url:       "http://localhost:8080/whip/ingress-abc",
				StreamKey: "sk-test-123",
			}, nil
		},
	}
}

// --- JoinSession tests ---

func TestJoinSession(t *testing.T) {
	tests := []struct {
		name           string
		role           string
		ingressErr     error
		wantErr        bool
		wantWhipURL    bool
		wantStreamKey  bool
		wantToken      bool
		wantLivekitURL bool
	}{
		{
			name:          "camera role returns WHIP credentials",
			role:          "camera",
			wantWhipURL:   true,
			wantStreamKey: true,
		},
		{
			name:           "director role returns JWT token",
			role:           "director",
			wantToken:      true,
			wantLivekitURL: true,
		},
		{
			name:       "camera role with ingress failure returns error",
			role:       "camera",
			ingressErr: errors.New("livekit unavailable"),
			wantErr:    true,
		},
		{
			name:    "invalid role returns error",
			role:    "admin",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			mock := defaultMockIngress()
			if tt.ingressErr != nil {
				mock.createFunc = func(_ *livekit.CreateIngressRequest) (*livekit.IngressInfo, error) {
					return nil, tt.ingressErr
				}
			}

			srv := newUnitTestServer(t, mock)
			seedSession(t, srv.store)

			reply, err := srv.JoinSession(context.Background(), &pb.JoinRequest{
				RoomCode: "ROOM-TEST",
				UserId:   "user-1",
				Role:     tt.role,
			})

			if (err != nil) != tt.wantErr {
				t.Fatalf("JoinSession() error = %v, wantErr %v", err, tt.wantErr)
			}
			if tt.wantErr {
				return
			}

			if tt.wantWhipURL && reply.WhipUrl == "" {
				t.Error("expected non-empty whip_url")
			}
			if tt.wantStreamKey && reply.StreamKey == "" {
				t.Error("expected non-empty stream_key")
			}
			if tt.wantToken && reply.Token == "" {
				t.Error("expected non-empty token")
			}
			if tt.wantLivekitURL && reply.LivekitUrl == "" {
				t.Error("expected non-empty livekit_url")
			}
			if !tt.wantWhipURL && reply.WhipUrl != "" {
				t.Error("unexpected whip_url in reply")
			}
			if !tt.wantToken && reply.Token != "" {
				t.Error("unexpected token in reply")
			}
		})
	}
}

// --- Ingress ID storage test ---

func TestJoinSession_CameraRole_StoresIngressID(t *testing.T) {
	srv := newUnitTestServer(t, defaultMockIngress())
	sess := seedSession(t, srv.store)

	_, err := srv.JoinSession(context.Background(), &pb.JoinRequest{
		RoomCode: "ROOM-TEST",
		UserId:   "camera-1",
		Role:     "camera",
	})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	ids, err := srv.store.GetIngressIDs(context.Background(), sess.ID)
	if err != nil {
		t.Fatalf("GetIngressIDs: %v", err)
	}
	if len(ids) != 1 || ids[0] != "ingress-abc" {
		t.Errorf("expected [ingress-abc] in Redis, got %v", ids)
	}
}

// --- CloseSession ingress cleanup tests ---

func TestCloseSession_IngressCleanup(t *testing.T) {
	tests := []struct {
		name            string
		ingressIDs      []string
		wantDeleteCount int
	}{
		{
			name:            "deletes all ingress IDs on close",
			ingressIDs:      []string{"ingress-1", "ingress-2"},
			wantDeleteCount: 2,
		},
		{
			name:            "no ingress IDs — close succeeds without delete calls",
			ingressIDs:      []string{},
			wantDeleteCount: 0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			mock := defaultMockIngress()
			srv := newUnitTestServer(t, mock)
			sess := seedSession(t, srv.store)

			for _, id := range tt.ingressIDs {
				if err := srv.store.AddIngressID(context.Background(), sess.ID, id); err != nil {
					t.Fatalf("AddIngressID(%s): %v", id, err)
				}
			}

			_, err := srv.CloseSession(context.Background(), &pb.CloseSessionRequest{
				RoomCode: "ROOM-TEST",
				UserId:   "director-1",
			})
			if err != nil {
				t.Fatalf("CloseSession: %v", err)
			}

			if len(mock.deletedIDs) != tt.wantDeleteCount {
				t.Errorf("expected %d DeleteIngress calls, got %d", tt.wantDeleteCount, len(mock.deletedIDs))
			}
		})
	}
}
