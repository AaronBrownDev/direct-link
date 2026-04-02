package signaling

import (
	"context"
	"errors"
	"fmt"
	"log/slog"

	"github.com/livekit/protocol/livekit"
)

type ingressClient interface {
	CreateIngress(ctx context.Context, req *livekit.CreateIngressRequest) (*livekit.IngressInfo, error)
	DeleteIngress(ctx context.Context, req *livekit.DeleteIngressRequest) (*livekit.IngressInfo, error)
}

type roomClient interface {
	DeleteRoom(ctx context.Context, req *livekit.DeleteRoomRequest) (*livekit.DeleteRoomResponse, error)
}

// DeleteRoom handles cleanup when a session is closed or expires, including deleting the LiveKit room and any associated ingresses.
func (s *Server) deleteRoom(ctx context.Context, sessionID string) error {
	var errs []error

	// Fetch and delete all ingress IDs associated with this session
	ingressIDs, err := s.store.GetIngressIDs(ctx, sessionID)

	if err != nil {
		s.logger.Warn("failed to get ingressIDs for cleanup", "session_ID", sessionID, "error", err)
		errs = append(errs, fmt.Errorf("fetch ingress IDs: %w", err))
	} else {
		for _, id := range ingressIDs {
			_, delErr := s.lkIngressClient.DeleteIngress(ctx, &livekit.DeleteIngressRequest{IngressId: id})
			if delErr != nil {
				s.logger.Warn("failed to delete ingressID", "error", delErr)
				errs = append(errs, fmt.Errorf("delete ingress %s: %w", id, delErr))
			} else {
				s.logger.Info("deleted ingress", "ingress_id", id, "session_id", sessionID)
			}
		}
	}

	// Delete the Livekit room itself (room name == sessionID)
	_, err = s.lkClient.DeleteRoom(ctx, &livekit.DeleteRoomRequest{Room: sessionID})
	if err != nil {
		s.logger.Warn("failed  to delete LiveKit room", "session_id", sessionID, "error", err,
			slog.String("hint", "room may have already been destroyed by LiveKit"))
		errs = append(errs, fmt.Errorf("delete room: %w", err))
	} else {
		s.logger.Info("deleted LiveKit room", "session_id", sessionID)
	}

	return errors.Join(errs...)
}
