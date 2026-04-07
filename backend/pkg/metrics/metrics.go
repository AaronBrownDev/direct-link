package metrics

import (
	"net/http"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

// Metrics holds all custom Prometheus metrics for the signaling server.
type Metrics struct {
	// Registry is the custom Prometheus registry.
	Registry *prometheus.Registry

	// Business metrics
	SessionsActive        prometheus.Gauge
	SessionsCreatedTotal  prometheus.Counter
	SessionsExpiredTotal  prometheus.Counter
	TokenGenerationsTotal *prometheus.CounterVec
	LatencyRequestsTotal  prometheus.Counter

	// Redis metrics
	RedisOperationDuration *prometheus.HistogramVec
	RedisErrorsTotal       *prometheus.CounterVec
}

// New creates a new Metrics instance with a custom registry.
// All metrics are registered on this isolated registry.
func New() *Metrics {
	reg := prometheus.NewRegistry()

	m := &Metrics{
		Registry: reg,

		SessionsActive: prometheus.NewGauge(prometheus.GaugeOpts{
			Namespace: "directlink",
			Name:      "sessions_active",
			Help:      "Number of currently active sessions.",
		}),

		SessionsCreatedTotal: prometheus.NewCounter(prometheus.CounterOpts{
			Namespace: "directlink",
			Name:      "sessions_created_total",
			Help:      "Total number of sessions created.",
		}),

		SessionsExpiredTotal: prometheus.NewCounter(prometheus.CounterOpts{
			Namespace: "directlink",
			Name:      "sessions_expired_total",
			Help:      "Total number of sessions expired by the janitor, due to TTL expiry.",
		}),

		TokenGenerationsTotal: prometheus.NewCounterVec(
			prometheus.CounterOpts{
				Namespace: "directlink",
				Name:      "token_generations_total",
				Help:      "Total LiveKit tokens generated, partitioned by role.",
			},
			[]string{"role"}, // "camera" or "director"
		),

		LatencyRequestsTotal: prometheus.NewCounter(prometheus.CounterOpts{
			Namespace: "directlink",
			Name:      "latency_time_requests_total",
			Help:      "Total number of GetServerTime RPC calls received.",
		}),

		RedisOperationDuration: prometheus.NewHistogramVec(
			prometheus.HistogramOpts{
				Namespace: "directlink",
				Name:      "redis_operation_duration_seconds",
				Help:      "Duration of Redis operations in seconds.",
				Buckets:   []float64{0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1},
			},
			[]string{"operation"},
		),

		RedisErrorsTotal: prometheus.NewCounterVec(
			prometheus.CounterOpts{
				Namespace: "directlink",
				Name:      "redis_errors_total",
				Help:      "Total Redis operation failures.",
			},
			[]string{"operation"},
		),
	}

	// Register Go runtime and process collectors.
	reg.MustRegister(
		collectors.NewGoCollector(),
		collectors.NewProcessCollector(collectors.ProcessCollectorOpts{}),
	)

	// Register all custom metrics
	reg.MustRegister(
		m.SessionsActive,
		m.SessionsCreatedTotal,
		m.TokenGenerationsTotal,
		m.LatencyRequestsTotal,
		m.RedisOperationDuration,
		m.RedisErrorsTotal,
	)

	return m
}

// Handler returns an HTTP handler that serves metrics from the custom registry.
func (m *Metrics) Handler() http.Handler {
	return promhttp.HandlerFor(m.Registry, promhttp.HandlerOpts{
		// Registry lets promhttp add its own scrape-tracking metrics
		// (promhttp_metric_handler_requests_total, etc.)
		Registry: m.Registry,
	})
}
