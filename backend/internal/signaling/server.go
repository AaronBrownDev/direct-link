package signaling

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"sync/atomic"
	"time"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/AaronBrownDev/direct-link/pkg/metrics"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	grpcprom "github.com/grpc-ecosystem/go-grpc-middleware/providers/prometheus"
	lksdk "github.com/livekit/server-sdk-go/v2"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"
)

type Server struct {
	cfg        Config
	httpServer *http.Server
	grpcServer *grpc.Server
	logger     *slog.Logger
	ready      atomic.Bool
	lkClient   *lksdk.RoomServiceClient
	store      session.Store
	metrics    *metrics.Metrics
	srvMetrics *grpcprom.ServerMetrics
	pb.UnimplementedSignalingServiceServer
}

// NewServer is a constructor for the signaling Server struct
func NewServer(cfg Config, logger *slog.Logger) *Server {

	s := &Server{
		cfg:    cfg,
		logger: logger,
	}

	// Create Redis store
	s.initStore()

	// Create and set metrics
	s.initMetrics()

	// Create new HTTP server and register
	s.initHTTP()

	// Create new gRPC server and register
	s.initGRPC()

	// Initialize LiveKit room service client
	s.initLiveKit()

	return s
}

func (s *Server) initStore() {

	store, err := session.NewRedisStore(
		s.cfg.RedisAddr,
		s.cfg.RedisPassword,
		s.cfg.RedisDB,
		s.cfg.RedisPoolSize,
		s.cfg.RedisMinIdle,
		s.cfg.RedisDialTimeout,
		s.cfg.RedisReadTimeout,
		s.cfg.RedisWriteTimeout,
		s.cfg.SessionTTL,
	)
	if err != nil {
		s.logger.Error("failed to create Redis store", "error", err)
		os.Exit(1) // TODO: look into if this exit is safe
	}

	s.store = store

}

func (s *Server) initMetrics() {

	// Create metrics
	s.metrics = metrics.New()

	// Create gRPC server metrics with latency histograms
	s.srvMetrics = grpcprom.NewServerMetrics(
		grpcprom.WithServerHandlingTimeHistogram(
			grpcprom.WithHistogramBuckets(
				[]float64{0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0},
			),
		),
	)
	s.metrics.Registry.MustRegister(s.srvMetrics)

	// Enable Redis instrumentation
	// TODO: s.store.SetMetrics(s.metrics)

}

func (s *Server) initHTTP() {

	mux := http.NewServeMux()
	s.registerRoutes(mux)

	s.httpServer = &http.Server{
		Addr:              fmt.Sprintf(":%d", s.cfg.HTTPPort),
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second, // TODO: should eventually put into config
	}

}

func (s *Server) initGRPC() {

	s.grpcServer = grpc.NewServer(
		grpc.ChainUnaryInterceptor(
			s.srvMetrics.UnaryServerInterceptor(),
		),
		grpc.ChainStreamInterceptor(
			s.srvMetrics.StreamServerInterceptor(),
		),
	)

	pb.RegisterSignalingServiceServer(s.grpcServer, s)

	// Needed for grpcurl testing
	reflection.Register(s.grpcServer)

	// Pre-populate metric labels for all registered RPCs
	s.srvMetrics.InitializeMetrics(s.grpcServer)

}

func (s *Server) initLiveKit() {

	s.lkClient = lksdk.NewRoomServiceClient(
		s.cfg.LiveKitHost,
		s.cfg.LiveKitAPIKey,
		s.cfg.LiveKitAPISecret,
	)

}

func (s *Server) registerRoutes(mux *http.ServeMux) {

	// Kubernetes probes
	mux.HandleFunc("GET /healthz", s.handleHealth)
	mux.HandleFunc("GET /readyz", s.handleReadiness)
	mux.HandleFunc("GET /livez", s.handleLiveness)

	// LiveKit
	mux.HandleFunc("POST /webhooks/livekit", s.handleLiveKitWebhook)

	// Prometheus metrics
	mux.Handle("GET /metrics", s.metrics.Handler())

}

// ListenAndServe starts the signaling server through http and gRPC
func (s *Server) ListenAndServe(ctx context.Context) error {

	// Create listeners for ports
	httpListener, err := net.Listen("tcp", fmt.Sprintf(":%d", s.cfg.HTTPPort))
	if err != nil {
		return fmt.Errorf("failed to create http listener: %w", err)
	}
	grpcListener, err := net.Listen("tcp", fmt.Sprintf(":%d", s.cfg.GRPCPort))
	if err != nil {
		return fmt.Errorf("failed to create grpc listener: %w", err)
	}

	// create error channel
	errCh := make(chan error, 2)

	go func() {
		errCh <- s.httpServer.Serve(httpListener)
	}()

	go func() {
		errCh <- s.grpcServer.Serve(grpcListener)
	}()

	// set server as ready to use and log it
	s.ready.Store(true)

	s.logger.Info("signaling server started", "grpc_port", s.cfg.GRPCPort, "http_port", s.cfg.HTTPPort)

	select {
	case err := <-errCh:
		return err
	case <-ctx.Done():
		return s.shutdown(context.Background()) //nolint:contextcheck // parent ctx is cancelled; fresh context needed for graceful shutdown
	}
}

// shutdown is a helper function for shutting down the grpc and http server gracefully.
func (s *Server) shutdown(ctx context.Context) error {

	s.ready.Store(false)

	s.logger.Info("signaling server shutdown gracefully")

	if s.store != nil {
		if err := s.store.Close(); err != nil {
			s.logger.Error("failed to close Redis store", "error", err)
		}
	}

	s.grpcServer.GracefulStop()

	httpCtx, cancel := context.WithTimeout(ctx, s.cfg.ShutdownTimeout)
	defer cancel()

	return s.httpServer.Shutdown(httpCtx)

}
