package metrics_test

import (
	"strings"
	"testing"

	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/prometheus/client_golang/prometheus/testutil"
)

func TestMetrics_New(t *testing.T) {
	m := metrics.New()

	if m.Registry == nil {
		t.Fatal("expected non-nil registry")
	}
	if m.Handler() == nil {
		t.Fatal("expected non-nil HTTP handler")
	}
}

func TestMetrics_SessionsActive(t *testing.T) {
	tests := []struct {
		name     string
		actions  func(m *metrics.Metrics)
		expected float64
	}{
		{
			name:     "starts at zero",
			actions:  func(_ *metrics.Metrics) {},
			expected: 0,
		},
		{
			name: "increments on session create",
			actions: func(m *metrics.Metrics) {
				m.SessionsActive.Inc()
			},
			expected: 1,
		},
		{
			name: "tracks multiple active sessions",
			actions: func(m *metrics.Metrics) {
				m.SessionsActive.Inc()
				m.SessionsActive.Inc()
				m.SessionsActive.Inc()
			},
			expected: 3,
		},
		{
			name: "decrements on session close",
			actions: func(m *metrics.Metrics) {
				m.SessionsActive.Inc()
				m.SessionsActive.Inc()
				m.SessionsActive.Dec()
			},
			expected: 1,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			m := metrics.New()
			tt.actions(m)

			got := testutil.ToFloat64(m.SessionsActive)
			if got != tt.expected {
				t.Errorf("sessions_active = %f, want %f", got, tt.expected)
			}
		})
	}
}

func TestMetrics_SessionsCreatedTotal(t *testing.T) {
	tests := []struct {
		name     string
		incCount int
		expected float64
	}{
		{
			name:     "starts at zero",
			incCount: 0,
			expected: 0,
		},
		{
			name:     "single creation",
			incCount: 1,
			expected: 1,
		},
		{
			name:     "multiple creations",
			incCount: 5,
			expected: 5,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			m := metrics.New()

			for range tt.incCount {
				m.SessionsCreatedTotal.Inc()
			}

			got := testutil.ToFloat64(m.SessionsCreatedTotal)
			if got != tt.expected {
				t.Errorf("sessions_created_total = %f, want %f", got, tt.expected)
			}
		})
	}
}

func TestMetrics_TokenGenerationsTotal(t *testing.T) {
	tests := []struct {
		name             string
		cameraTokens     int
		directorTokens   int
		expectedCamera   float64
		expectedDirector float64
	}{
		{
			name:             "no tokens generated",
			cameraTokens:     0,
			directorTokens:   0,
			expectedCamera:   0,
			expectedDirector: 0,
		},
		{
			name:             "camera tokens only",
			cameraTokens:     3,
			directorTokens:   0,
			expectedCamera:   3,
			expectedDirector: 0,
		},
		{
			name:             "director tokens only",
			cameraTokens:     0,
			directorTokens:   2,
			expectedCamera:   0,
			expectedDirector: 2,
		},
		{
			name:             "mixed roles tracked independently",
			cameraTokens:     3,
			directorTokens:   1,
			expectedCamera:   3,
			expectedDirector: 1,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			m := metrics.New()

			for range tt.cameraTokens {
				m.TokenGenerationsTotal.WithLabelValues("camera").Inc()
			}
			for range tt.directorTokens {
				m.TokenGenerationsTotal.WithLabelValues("director").Inc()
			}

			gotCamera := testutil.ToFloat64(m.TokenGenerationsTotal.WithLabelValues("camera"))
			if gotCamera != tt.expectedCamera {
				t.Errorf("camera token count = %f, want %f", gotCamera, tt.expectedCamera)
			}

			gotDirector := testutil.ToFloat64(m.TokenGenerationsTotal.WithLabelValues("director"))
			if gotDirector != tt.expectedDirector {
				t.Errorf("director token count = %f, want %f", gotDirector, tt.expectedDirector)
			}
		})
	}
}

func TestMetrics_RedisErrorsTotal(t *testing.T) {
	tests := []struct {
		name      string
		operation string
		incCount  int
		expected  float64
	}{
		{
			name:      "no errors recorded",
			operation: "create_session",
			incCount:  0,
			expected:  0,
		},
		{
			name:      "single error",
			operation: "get_session",
			incCount:  1,
			expected:  1,
		},
		{
			name:      "multiple errors same operation",
			operation: "ping",
			incCount:  3,
			expected:  3,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			m := metrics.New()

			for range tt.incCount {
				m.RedisErrorsTotal.WithLabelValues(tt.operation).Inc()
			}

			got := testutil.ToFloat64(m.RedisErrorsTotal.WithLabelValues(tt.operation))
			if got != tt.expected {
				t.Errorf("redis_errors_total{operation=%q} = %f, want %f", tt.operation, got, tt.expected)
			}
		})
	}
}

func TestMetrics_RegistryContainsAllMetrics(t *testing.T) {
	m := metrics.New()

	// Initialize vec metrics so they appear in Gather
	m.TokenGenerationsTotal.WithLabelValues("camera")
	m.RedisOperationDuration.WithLabelValues("ping")
	m.RedisErrorsTotal.WithLabelValues("ping")

	families, err := m.Registry.Gather()
	if err != nil {
		t.Fatalf("Registry.Gather() failed: %v", err)
	}

	gathered := make(map[string]bool)
	for _, f := range families {
		gathered[f.GetName()] = true
	}

	tests := []struct {
		name       string
		metricName string
	}{
		{"sessions active gauge", "directlink_sessions_active"},
		{"sessions created counter", "directlink_sessions_created_total"},
		{"token generations counter vec", "directlink_token_generations_total"},
		{"redis operation duration histogram", "directlink_redis_operation_duration_seconds"},
		{"redis errors counter vec", "directlink_redis_errors_total"},
		{"go goroutines collector", "go_goroutines"},
		{"go memory collector", "go_memstats_alloc_bytes"},
	}

	var missing []string
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if !gathered[tt.metricName] {
				missing = append(missing, tt.metricName)
				t.Errorf("metric %q not found in registry", tt.metricName)
			}
		})
	}

	if len(missing) > 0 {
		t.Errorf("registry missing metrics: %s", strings.Join(missing, ", "))
	}
}
