package signaling

import (
	"context"
	"fmt"
	"time"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/livekit/protocol/auth"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// JoinSession authenticates a user and returns a LiveKit access token.
// The client uses this token to connect directly to LiveKit for media.
func (s *Server) JoinSession(ctx context.Context, req *pb.JoinRequest) (*pb.JoinReply, error) {

	// Validate required fields
	if req.SessionId == "" {
		return nil, status.Error(codes.InvalidArgument, "session_id is required")
	}
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}
	if req.Role == "" {
		return nil, status.Error(codes.InvalidArgument, "role is required")
	}

	// Determine permissions based on role
	canPublish, canSubscribe, err := permissionsForRole(req.Role)
	if err != nil {
		return nil, status.Error(codes.InvalidArgument, err.Error())
	}

	s.logger.Info("generating LiveKit token",
		"session_id", req.SessionId,
		"user_id", req.UserId,
		"role", req.Role,
	)

	// Build LiveKit access token with role-based permissions
	at := auth.NewAccessToken(s.cfg.LiveKitAPIKey, s.cfg.LiveKitAPISecret)

	grant := &auth.VideoGrant{
		RoomJoin:     true,
		Room:         req.SessionId,
		CanPublish:   &canPublish,
		CanSubscribe: &canSubscribe,
	}

	at.SetVideoGrant(grant).
		SetIdentity(req.UserId).
		SetValidFor(time.Hour)

	token, err := at.ToJWT()
	if err != nil {
		s.logger.Error("failed to generate token", "error", err)
		return nil, status.Error(codes.Internal, "failed to generate access token")
	}

	s.logger.Info("peer joined session",
		"session_id", req.SessionId,
		"user_id", req.UserId,
		"role", req.Role,
	)

	return &pb.JoinReply{
		Token:      token,
		LivekitUrl: s.cfg.LiveKitHost,
	}, nil
}

// permissionsForRole maps a DirectLink role to LiveKit publish/subscribe permissions.
func permissionsForRole(role string) (canPublish bool, canSubscribe bool, err error) {
	switch role {
	case "camera":
		return true, false, nil
	case "director":
		return false, true, nil
	default:
		return false, false, fmt.Errorf("unknown role %q: must be \"camera\" or \"director\"", role)
	}
}
