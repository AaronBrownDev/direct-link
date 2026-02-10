package signaling

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"sync/atomic"

	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	lksdk "github.com/livekit/server-sdk-go/v2"
)

type Server struct {
	cfg        Config
	httpServer *http.Server
	grpcServer *grpc.Server
	logger     *slog.Logger
	ready      atomic.Bool
	lkClient   *lksdk.RoomServiceClient
	pb.UnimplementedSignalingServiceServer
}

// NewServer is a constructor for the signaling Server struct
func NewServer(cfg Config, logger *slog.Logger) *Server {

	server := &Server{
		cfg:    cfg,
		logger: logger,
	}

	// Create new HTTP server and register
	mux := http.NewServeMux()
	server.registerRoutes(mux)

	server.httpServer = &http.Server{
		Addr:    fmt.Sprintf(":%d", cfg.HTTPPort),
		Handler: mux,
	}

	// Create new gRPC server and register
	server.grpcServer = grpc.NewServer()
	pb.RegisterSignalingServiceServer(server.grpcServer, server)

	// Needed for grpcurl testing
	reflection.Register(server.grpcServer)

	// Initialize LiveKit room service client
	server.lkClient = lksdk.NewRoomServiceClient(
		cfg.LiveKitHost,
		cfg.LiveKitAPIKey,
		cfg.LiveKitAPISecret,
	)

	return server
}

func (s *Server) registerRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /healthz", s.handleHealth)
	mux.HandleFunc("GET /readyz", s.handleReadiness)
	mux.HandleFunc("GET /livez", s.handleLiveness)
}

// ListenAndServe starts the signaling server through http and gRPC
func (s *Server) ListenAndServe(ctx context.Context) error {

	// Create listeners for ports
	httpListener, err := net.Listen("tcp", fmt.Sprintf(":%d", s.cfg.HTTPPort))
	if err != nil {
		return fmt.Errorf("failed to create http listener: %v", err)
	}
	grpcListener, err := net.Listen("tcp", fmt.Sprintf(":%d", s.cfg.GRPCPort))
	if err != nil {
		return fmt.Errorf("failed to create grpc listener: %v", err)
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
		return s.shutdown()
	}
}

// shutdown is a helper function for shutting down the grpc and http server gracefully.
func (s *Server) shutdown() error {

	s.ready.Store(false)

	s.logger.Info("signaling server shutdown gracefully")

	s.grpcServer.GracefulStop()

	httpCtx, cancel := context.WithTimeout(context.Background(), s.cfg.ShutdownTimeout)
	defer cancel()

	return s.httpServer.Shutdown(httpCtx)

}
