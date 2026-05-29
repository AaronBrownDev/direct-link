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
	"github.com/prometheus/client_golang/prometheus/testutil"
)

// --- Stub Store ---

// stubStore implements session.Store with overridable function fields.
// Unset fields return zero values; only the methods exercised by a given test
// need to be populated.
type stubStore struct {
	getSessionByRoomCodeFunc func(ctx context.Context, code string) (*session.Session, error)
	grantAccessFunc          func(ctx context.Context, sessionID, userID, role string) error
	addIngressIDFunc         func(ctx context.Context, sessionID, ingressID string) error
	getIngressIDsFunc        func(ctx context.Context, sessionID string) ([]string, error)
	updateSessionStatusFunc  func(ctx context.Context, sessionID, status string) error
}

func (s *stubStore) GetSessionByRoomCode(ctx context.Context, code string) (*session.Session, error) {
	if s.getSessionByRoomCodeFunc != nil {
		return s.getSessionByRoomCodeFunc(ctx, code)
	}
	return nil, session.ErrSessionNotFound
}

func (s *stubStore) GrantAccess(ctx context.Context, sessionID, userID, role string) error {
	if s.grantAccessFunc != nil {
		return s.grantAccessFunc(ctx, sessionID, userID, role)
	}
	return nil
}

func (s *stubStore) AddIngressID(ctx context.Context, sessionID, ingressID string) error {
	if s.addIngressIDFunc != nil {
		return s.addIngressIDFunc(ctx, sessionID, ingressID)
	}
	return nil
}

func (s *stubStore) GetIngressIDs(ctx context.Context, sessionID string) ([]string, error) {
	if s.getIngressIDsFunc != nil {
		return s.getIngressIDsFunc(ctx, sessionID)
	}
	return nil, nil
}

func (s *stubStore) UpdateSessionStatus(ctx context.Context, sessionID, status string) error {
	if s.updateSessionStatusFunc != nil {
		return s.updateSessionStatusFunc(ctx, sessionID, status)
	}
	return nil
}

func (s *stubStore) CreateSession(_ context.Context, _ *session.Session) error { return nil }
func (s *stubStore) GetSession(_ context.Context, _ string) (*session.Session, error) {
	return nil, nil
}
func (s *stubStore) DeleteSession(_ context.Context, _ string) error { return nil }
func (s *stubStore) GetExpiredSessions(_ context.Context, _ time.Time) ([]session.Session, error) {
	return nil, nil
}
func (s *stubStore) GetRole(_ context.Context, _, _ string) (string, error) { return "", nil }
func (s *stubStore) RevokeAccess(_ context.Context, _, _ string) error      { return nil }
func (s *stubStore) HasAccess(_ context.Context, _, _ string) (bool, error) { return false, nil }
func (s *stubStore) GetUserSessions(_ context.Context, _ string) ([]session.Session, error) {
	return nil, nil
}
func (s *stubStore) Ping(_ context.Context) error { return nil }
func (s *stubStore) Close() error                 { return nil }

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

type mockRoomClient struct {
	deleteFunc func(*livekit.DeleteRoomRequest) (*livekit.DeleteRoomResponse, error)
	deletedIDs []string
}

func (m *mockRoomClient) DeleteRoom(_ context.Context, req *livekit.DeleteRoomRequest) (*livekit.DeleteRoomResponse, error) {
	m.deletedIDs = append(m.deletedIDs, req.Room)
	if m.deleteFunc != nil {
		return m.deleteFunc(req)
	}
	return &livekit.DeleteRoomResponse{}, nil
}

// --- Helpers ---

func newUnitTestServer(t *testing.T, mockIngress *mockIngressClient, mockRoom *mockRoomClient) *Server {
	t.Helper()

	mr := miniredis.RunT(t)
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

	return &Server{
		cfg:             DefaultConfig(),
		store:           store,
		logger:          slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError})),
		lkIngressClient: mockIngress,
		lkClient:        mockRoom,
		metrics:         metrics.New(),
	}
}

func seedSession(t *testing.T, store session.Store) *session.Session {
	t.Helper()
	sess := &session.Session{
		ID: "test-session-id", RoomCode: "ROOM-123456", CreatedBy: "director-1",
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

func defaultMockRoom() *mockRoomClient {
	return &mockRoomClient{}
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
			mockIngress := defaultMockIngress()
			mockRoom := defaultMockRoom()
			if tt.ingressErr != nil {
				mockIngress.createFunc = func(_ *livekit.CreateIngressRequest) (*livekit.IngressInfo, error) {
					return nil, tt.ingressErr
				}
			}

			srv := newUnitTestServer(t, mockIngress, mockRoom)
			seedSession(t, srv.store)

			reply, err := srv.JoinSession(context.Background(), &pb.JoinRequest{
				RoomCode: "ROOM-123456",
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
	srv := newUnitTestServer(t, defaultMockIngress(), defaultMockRoom())
	sess := seedSession(t, srv.store)

	_, err := srv.JoinSession(context.Background(), &pb.JoinRequest{
		RoomCode: "ROOM-123456",
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

// Verifies that if GrantAccess fails after a successful CreateIngress, the ingress is deleted
// and no partial state is left in Redis.
func TestJoinSession_CameraRole_RollsBackIngressOnGrantAccessFailure(t *testing.T) {
	seededSession := &session.Session{
		ID: "test-session-id", RoomCode: "ROOM-123456", CreatedBy: "director-1",
		CreatedAt: time.Now().UTC(), MaxCameras: 4, Status: "active",
	}
	grantErr := errors.New("simulated GrantAccess failure")

	store := &stubStore{
		getSessionByRoomCodeFunc: func(_ context.Context, _ string) (*session.Session, error) {
			return seededSession, nil
		},
		grantAccessFunc: func(_ context.Context, _, _, _ string) error {
			return grantErr
		},
	}

	mockIngress := defaultMockIngress()
	srv := &Server{
		cfg:             Config{LiveKitHost: "http://localhost:7880", LiveKitExternalURL: "ws://localhost:7880", LiveKitAPIKey: "devkey", LiveKitAPISecret: "secret"},
		store:           store,
		logger:          slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError})),
		lkIngressClient: mockIngress,
		lkClient:        defaultMockRoom(),
		metrics:         metrics.New(),
	}

	_, err := srv.JoinSession(context.Background(), &pb.JoinRequest{
		RoomCode: seededSession.RoomCode,
		UserId:   "camera-rollback",
		Role:     "camera",
	})

	if err == nil {
		t.Fatal("expected error when GrantAccess fails")
	}

	// The ingress should have been rolled back via DeleteIngress
	if len(mockIngress.deletedIDs) != 1 || mockIngress.deletedIDs[0] != "ingress-abc" {
		t.Errorf("expected ingress-abc to be rolled back, got deletedIDs=%v", mockIngress.deletedIDs)
	}
}

// --- GetServerTime tests ---

func TestGetServerTime(t *testing.T) {
	tests := []struct {
		name      string
		callCount int
	}{
		{
			name:      "returns non-zero timestamp",
			callCount: 1,
		},
		{
			name:      "timestamp within 1ms of call time",
			callCount: 1,
		},
		{
			name:      "increments counter on each call",
			callCount: 3,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			srv := newUnitTestServer(t, defaultMockIngress(), defaultMockRoom())

			var reply *pb.GetServerTimeReply
			var err error
			before := time.Now()
			for range tt.callCount {
				reply, err = srv.GetServerTime(context.Background(), &pb.GetServerTimeRequest{})
			}
			after := time.Now()

			if err != nil {
				t.Fatalf("GetServerTime() unexpected error: %v", err)
			}
			if reply == nil {
				t.Fatal("GetServerTime() returned nil reply")
			}

			gotNs := reply.ServerTimeNs
			if gotNs == 0 {
				t.Error("ServerTimeNs is zero, expected a real Unix timestamp")
			}

			const toleranceNs = int64(time.Millisecond)
			if gotNs < before.UnixNano()-toleranceNs || gotNs > after.UnixNano()+toleranceNs {
				t.Errorf("ServerTimeNs %d is outside [%d, %d] (±1ms window around call)",
					gotNs, before.UnixNano(), after.UnixNano())
			}

			got := testutil.ToFloat64(srv.metrics.LatencyRequestsTotal)
			if got != float64(tt.callCount) {
				t.Errorf("latency_time_requests_total = %f, want %f", got, float64(tt.callCount))
			}
		})
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
			mockIngress := defaultMockIngress()
			mockRoom := defaultMockRoom()
			srv := newUnitTestServer(t, mockIngress, mockRoom)
			sess := seedSession(t, srv.store)

			for _, id := range tt.ingressIDs {
				if err := srv.store.AddIngressID(context.Background(), sess.ID, id); err != nil {
					t.Fatalf("AddIngressID(%s): %v", id, err)
				}
			}

			_, err := srv.CloseSession(context.Background(), &pb.CloseSessionRequest{
				RoomCode: "ROOM-123456",
				UserId:   "director-1",
			})
			if err != nil {
				t.Fatalf("CloseSession: %v", err)
			}

			if len(mockIngress.deletedIDs) != tt.wantDeleteCount {
				t.Errorf("expected %d DeleteIngress calls, got %d", tt.wantDeleteCount, len(mockIngress.deletedIDs))
			}

			// Verify the LiveKit room was also deleted
			if len(mockRoom.deletedIDs) != 1 || mockRoom.deletedIDs[0] != sess.ID {
				t.Errorf("expected DeleteRoom called with %q, got %v", sess.ID, mockRoom.deletedIDs)
			}
		})
	}
}
