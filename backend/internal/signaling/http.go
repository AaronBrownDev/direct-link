package signaling

import (
	"encoding/json"
	"net/http"
)

// handleHealth is for doing a general health check
func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	s.writeJSON(w, http.StatusOK, map[string]string{
		"status":  "available",
		"service": "signaling",
	})
}

// handleReadiness is for seeing if signaling server can handle traffic
func (s *Server) handleReadiness(w http.ResponseWriter, r *http.Request) {
	if s.ready.Load() {
		s.writeJSON(w, http.StatusOK, map[string]string{
			"status":  "ready",
			"service": "signaling",
		})
	} else {
		s.writeJSON(w, http.StatusServiceUnavailable, map[string]string{
			"status":  "not_ready",
			"service": "signaling",
		})
	}
}

// handleLiveness is for checking if the process is alive
func (s *Server) handleLiveness(w http.ResponseWriter, r *http.Request) {
	s.writeJSON(w, http.StatusOK, map[string]string{
		"status":  "alive",
		"service": "signaling",
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
