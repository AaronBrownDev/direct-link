package sfu

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"net/http"
	"sync/atomic"

	"google.golang.org/grpc"
)

type Server struct {
	cfg        Config
	httpServer *http.Server
	grpcServer *grpc.Server
	logger     *slog.Logger
	ready      atomic.Bool
	// TODO: sfu *sfu.SFU will be added when integrating ion-sfu
}

// NewServer is a constructor for the sfu Server struct
func NewServer(cfg Config, logger *slog.Logger) *Server {

	server := &Server{
		cfg:    cfg,
		logger: logger,
	}

	mux := http.NewServeMux()
	server.registerRoutes(mux)

	server.httpServer = &http.Server{
		Addr:    fmt.Sprintf(":%d", cfg.HTTPPort),
		Handler: mux,
	}

	return server
}

func (s *Server) registerRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /healthz", s.handleHealth)
	mux.HandleFunc("GET /readyz", s.handleReadiness)
	mux.HandleFunc("GET /livez", s.handleLiveness)
}

// ListenAndServe starts the sfu server through http and gRPC
func (s *Server) ListenAndServe(ctx context.Context) error {
	// TODO: need to generate proto go files to add logic
	return nil
}

// handleHealth is for doing a general health check
func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	s.writeJSON(w, http.StatusOK, map[string]string{
		"status":  "available",
		"service": "sfu",
	})
}

// handleReadiness is for seeing if sfu server can handle traffic
func (s *Server) handleReadiness(w http.ResponseWriter, r *http.Request) {
	if s.ready.Load() {
		s.writeJSON(w, http.StatusOK, map[string]string{
			"status":  "ready",
			"service": "sfu",
		})
	} else {
		s.writeJSON(w, http.StatusServiceUnavailable, map[string]string{
			"status":  "not_ready",
			"service": "sfu",
		})
	}
}

// handleLiveness is for checking if the process is alive
func (s *Server) handleLiveness(w http.ResponseWriter, r *http.Request) {
	s.writeJSON(w, http.StatusOK, map[string]string{
		"status":  "alive",
		"service": "sfu",
	})
}

// writeJSON is a helper func for http handlers
func (s *Server) writeJSON(w http.ResponseWriter, status int, data map[string]string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(data); err != nil {
		s.logger.Error("failed to encode response", "error", err)
	}
}
