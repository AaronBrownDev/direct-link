package session_test

import (
	"context"
	"testing"
	"time"

	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/alicebob/miniredis/v2"
	"github.com/prometheus/client_golang/prometheus/testutil"
)

// newMetricsTestStore creates a RedisStore backed by miniredis with metrics enabled.
func newMetricsTestStore(t *testing.T) (*session.RedisStore, *metrics.Metrics, *miniredis.Miniredis) {
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

	m := metrics.New()
	store.SetMetrics(m)

	return store, m, mr
}

func newMetricsSession(id, roomCode, owner string) *session.Session {
	return &session.Session{
		ID:         id,
		RoomCode:   roomCode,
		CreatedBy:  owner,
		CreatedAt:  time.Now(),
		MaxCameras: 4,
		Status:     "active",
	}
}

// histogramCount returns the total observation count for a given metric name
// and operation label from the registry.
func histogramCount(t *testing.T, m *metrics.Metrics, metricName, operation string) uint64 {
	t.Helper()

	families, err := m.Registry.Gather()
	if err != nil {
		t.Fatalf("Registry.Gather() failed: %v", err)
	}

	for _, f := range families {
		if f.GetName() != metricName {
			continue
		}
		for _, metric := range f.GetMetric() {
			for _, label := range metric.GetLabel() {
				if label.GetName() == "operation" && label.GetValue() == operation {
					return metric.GetHistogram().GetSampleCount()
				}
			}
		}
	}
	return 0
}

// Tests that Redis operations without metrics set do not panic
func TestRedisMetrics_NilSafety(t *testing.T) {
	mr := miniredis.RunT(t)

	store, err := session.NewRedisStore(
		mr.Addr(), "", 0, 10, 2,
		time.Second, time.Second, time.Second,
		24*time.Hour,
	)
	if err != nil {
		t.Fatalf("failed to create store: %v", err)
	}
	defer store.Close()

	// Do NOT call SetMetrics — metrics remain nil
	ctx := context.Background()

	tests := []struct {
		name   string
		action func() error
	}{
		{
			name: "CreateSession without metrics",
			action: func() error {
				return store.CreateSession(ctx, newMetricsSession("nil-1", "NIL-001", "d-nil"))
			},
		},
		{
			name: "GetSession without metrics",
			action: func() error {
				_, err := store.GetSession(ctx, "nil-1")
				return err
			},
		},
		{
			name: "Ping without metrics",
			action: func() error {
				return store.Ping(ctx)
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if err := tt.action(); err != nil {
				t.Errorf("expected no error without metrics, got %v", err)
			}
		})
	}
}

// Tests that each Redis operation records a duration histogram observation
func TestRedisMetrics_OperationDuration(t *testing.T) {
	tests := []struct {
		name      string
		operation string
		setup     func(t *testing.T, store *session.RedisStore, ctx context.Context)
		action    func(t *testing.T, store *session.RedisStore, ctx context.Context)
		expected  uint64
	}{
		{
			name:      "CreateSession records duration",
			operation: "create_session",
			setup:     func(_ *testing.T, _ *session.RedisStore, _ context.Context) {},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-1", "DUR-001", "d-create")); err != nil {
					t.Fatalf("CreateSession: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "GetSession records duration",
			operation: "get_session",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-2", "DUR-002", "d-get")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if _, err := store.GetSession(ctx, "dur-2"); err != nil {
					t.Fatalf("GetSession: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "GetSessionByRoomCode records duration",
			operation: "get_session_by_room_code",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-3", "DUR-003", "d-1")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if _, err := store.GetSessionByRoomCode(ctx, "DUR-003"); err != nil {
					t.Fatalf("GetSessionByRoomCode: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "UpdateSessionStatus records duration",
			operation: "update_session_status",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-4", "DUR-004", "d-1")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.UpdateSessionStatus(ctx, "dur-4", "closed"); err != nil {
					t.Fatalf("UpdateSessionStatus: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "DeleteSession records duration",
			operation: "delete_session",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-5", "DUR-005", "d-1")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.DeleteSession(ctx, "dur-5"); err != nil {
					t.Fatalf("DeleteSession: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "Ping records duration",
			operation: "ping",
			setup:     func(_ *testing.T, _ *session.RedisStore, _ context.Context) {},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.Ping(ctx); err != nil {
					t.Fatalf("Ping: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "GrantAccess records duration",
			operation: "grant_access",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-6", "DUR-006", "d-grant")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.GrantAccess(ctx, "dur-6", "cam-1", "camera"); err != nil {
					t.Fatalf("GrantAccess: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "HasAccess records duration",
			operation: "has_access",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-7", "DUR-007", "d-access")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
				if err := store.GrantAccess(ctx, "dur-7", "cam-1", "camera"); err != nil {
					t.Fatalf("setup GrantAccess: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if _, err := store.HasAccess(ctx, "dur-7", "cam-1"); err != nil {
					t.Fatalf("HasAccess: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "GetRole records duration",
			operation: "get_role",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-8", "DUR-008", "d-role")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
				if err := store.GrantAccess(ctx, "dur-8", "cam-1", "camera"); err != nil {
					t.Fatalf("setup GrantAccess: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if _, err := store.GetRole(ctx, "dur-8", "cam-1"); err != nil {
					t.Fatalf("GetRole: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "RevokeAccess records duration",
			operation: "revoke_access",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-9", "DUR-009", "d-revoke")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
				if err := store.GrantAccess(ctx, "dur-9", "cam-1", "camera"); err != nil {
					t.Fatalf("setup GrantAccess: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.RevokeAccess(ctx, "dur-9", "cam-1"); err != nil {
					t.Fatalf("RevokeAccess: %v", err)
				}
			},
			expected: 1,
		},
		{
			name:      "GetUserSessions records duration",
			operation: "get_user_sessions",
			setup: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.CreateSession(ctx, newMetricsSession("dur-10", "DUR-010", "d-sessions")); err != nil {
					t.Fatalf("setup CreateSession: %v", err)
				}
			},
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if _, err := store.GetUserSessions(ctx, "d-sessions"); err != nil {
					t.Fatalf("GetUserSessions: %v", err)
				}
			},
			expected: 1,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			store, m, _ := newMetricsTestStore(t)
			defer store.Close()
			ctx := context.Background()

			tt.setup(t, store, ctx)
			tt.action(t, store, ctx)

			count := histogramCount(t, m, "directlink_redis_operation_duration_seconds", tt.operation)
			if count != tt.expected {
				t.Errorf("duration observations for %q = %d, want %d", tt.operation, count, tt.expected)
			}
		})
	}
}

// Tests that application-level errors are NOT counted as Redis infrastructure errors
func TestRedisMetrics_ErrorFiltering(t *testing.T) {
	tests := []struct {
		name      string
		operation string
		action    func(t *testing.T, store *session.RedisStore, ctx context.Context)
		wantError float64
	}{
		{
			name:      "ErrSessionNotFound does not increment error counter",
			operation: "get_session",
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				_, err := store.GetSession(ctx, "nonexistent")
				if err == nil {
					t.Fatal("expected error")
				}
			},
			wantError: 0,
		},
		{
			name:      "ErrInvalidRoomCode does not increment error counter",
			operation: "get_session_by_room_code",
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				_, err := store.GetSessionByRoomCode(ctx, "INVALID-CODE")
				if err == nil {
					t.Fatal("expected error")
				}
			},
			wantError: 0,
		},
		{
			name:      "successful operation does not increment error counter",
			operation: "ping",
			action: func(t *testing.T, store *session.RedisStore, ctx context.Context) {
				if err := store.Ping(ctx); err != nil {
					t.Fatalf("Ping: %v", err)
				}
			},
			wantError: 0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			store, m, _ := newMetricsTestStore(t)
			defer store.Close()
			ctx := context.Background()

			tt.action(t, store, ctx)

			got := testutil.ToFloat64(m.RedisErrorsTotal.WithLabelValues(tt.operation))
			if got != tt.wantError {
				t.Errorf("redis_errors_total{operation=%q} = %f, want %f", tt.operation, got, tt.wantError)
			}
		})
	}
}

// Tests that actual Redis infrastructure failures DO increment the error counter
func TestRedisMetrics_InfrastructureError(t *testing.T) {
	tests := []struct {
		name      string
		operation string
		action    func(t *testing.T, store *session.RedisStore, ctx context.Context)
		wantError float64
	}{
		{
			name:      "Ping failure increments error counter",
			operation: "ping",
			action: func(_ *testing.T, store *session.RedisStore, ctx context.Context) {
				_ = store.Ping(ctx)
			},
			wantError: 1,
		},
		{
			name:      "GetSession failure increments error counter",
			operation: "get_session",
			action: func(_ *testing.T, store *session.RedisStore, ctx context.Context) {
				_, _ = store.GetSession(ctx, "any-id")
			},
			wantError: 1,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			store, m, mr := newMetricsTestStore(t)
			defer store.Close()
			ctx := context.Background()

			// Close miniredis to simulate infrastructure failure
			mr.Close()

			tt.action(t, store, ctx)

			got := testutil.ToFloat64(m.RedisErrorsTotal.WithLabelValues(tt.operation))
			if got != tt.wantError {
				t.Errorf("redis_errors_total{operation=%q} = %f, want %f", tt.operation, got, tt.wantError)
			}
		})
	}
}

// Tests that multiple operations accumulate duration observations correctly
func TestRedisMetrics_MultipleOperations(t *testing.T) {
	store, m, _ := newMetricsTestStore(t)
	defer store.Close()
	ctx := context.Background()

	iterations := 5
	for i := range iterations {
		sess := newMetricsSession(
			"multi-"+string(rune('a'+i)),
			"MULTI-"+string(rune('A'+i)),
			"d-multi-"+string(rune('a'+i)),
		)
		if err := store.CreateSession(ctx, sess); err != nil {
			t.Fatalf("CreateSession iteration %d: %v", i, err)
		}
	}

	count := histogramCount(t, m, "directlink_redis_operation_duration_seconds", "create_session")
	if count != uint64(iterations) {
		t.Errorf("duration observations for create_session = %d, want %d", count, iterations)
	}
}
