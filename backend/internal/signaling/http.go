package signaling

import (
	"encoding/json"
	"net/http"

	"github.com/livekit/protocol/auth"
	"github.com/livekit/protocol/webhook"
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

// handleLiveKitWebhook receives event notification from LiveKit.
func (s *Server) handleLiveKitWebhook(w http.ResponseWriter, r *http.Request) {

	authProvider := auth.NewSimpleKeyProvider(s.cfg.LiveKitAPIKey, s.cfg.LiveKitAPISecret)

	event, err := webhook.ReceiveWebhookEvent(r, authProvider)
	if err != nil {
		s.logger.Error("webhook validation failed", "error", err)
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	switch event.GetEvent() {
	case "participant_joined":
		s.logger.Info(
			"participant joined",
			"room", event.GetRoom().GetName(),
			"identity", event.GetParticipant().GetIdentity(),
		)
	case "participant_left":
		s.logger.Info(
			"participant left",
			"room", event.GetRoom().GetName(),
			"identity", event.GetParticipant().GetIdentity(),
		)
	case "track_published":
		s.logger.Info(
			"track published",
			"room", event.GetRoom().GetName(),
			"identity", event.GetParticipant().GetIdentity(),
		)
	case "room_finished":
		s.logger.Info(
			"room finished",
			"room", event.GetRoom().GetName(),
		)
	default:
		s.logger.Debug("unhandled webhook event", "event", event.GetEvent())
	}

	w.WriteHeader(http.StatusOK)
}

// writeJSON is a helper func for http handlers
func (s *Server) writeJSON(w http.ResponseWriter, status int, data map[string]string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(data); err != nil {
		s.logger.Error("failed to encode response", "error", err)
	}
}
