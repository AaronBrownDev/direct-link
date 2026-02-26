package signaling

import (
	"context"
	"fmt"
	"time"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/livekit/protocol/auth"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// JoinSession authenticates a user and returns a LiveKit access token.
// The client uses this token to connect directly to LiveKit for media.
func (s *Server) JoinSession(ctx context.Context, req *pb.JoinRequest) (*pb.JoinReply, error) {

	// Validate required fields
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}
	if req.Role == "" {
		return nil, status.Error(codes.InvalidArgument, "role is required")
	}
	if req.RoomCode == "" {
		return nil, status.Error(codes.InvalidArgument, "room_code is required")
	}

	// Resolve room code to session and verify it is active
	sess, err := s.store.GetSessionByRoomCode(ctx, req.RoomCode)
	if err != nil {
		s.logger.Error("room code lookup failed", "room_code", req.RoomCode, "error", err)
		return nil, status.Error(codes.NotFound, "session not found")
	}
	if sess.Status == "closed" {
		return nil, status.Error(codes.FailedPrecondition, "session is closed")
	}

	// Auto-grant acccess for valid room code join
	if err := s.store.GrantAccess(ctx, sess.ID, req.UserId, req.Role); err != nil {
		s.logger.Error("failed to grant access", "error", err)
		return nil, status.Error(codes.Internal, "failed to grant access")
	}

	// Determine permissions based on role
	canPublish, canSubscribe, err := permissionsForRole(req.Role)
	if err != nil {
		return nil, status.Error(codes.InvalidArgument, err.Error())
	}

	// Resolves final session ID when joining my req.RoomCode
	s.logger.Info("generating LiveKit token",
		"session_id", sess.ID,
		"user_id", req.UserId,
		"role", req.Role,
	)

	// Build LiveKit access token with role-based permissions
	at := auth.NewAccessToken(s.cfg.LiveKitAPIKey, s.cfg.LiveKitAPISecret)

	grant := &auth.VideoGrant{
		RoomJoin:     true,
		Room:         sess.ID,
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
		"session_id", sess.ID,
		"user_id", req.UserId,
		"role", req.Role,
	)

	return &pb.JoinReply{
		Token:      token,
		LivekitUrl: s.cfg.LiveKitHost,
	}, nil
}

// Creates a new production session and returns a room code
func (s *Server) CreateSession(ctx context.Context, req *pb.CreateSessionRequest) (*pb.CreateSessionReply, error) {
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}
	if req.MaxCameras <= 0 {
		//TODO: Add camera limit
		return nil, status.Error(codes.InvalidArgument, "max_cameras must be greater than zero")
	}

	// Generate session ID and room code
	sessionID := session.NewSessionID()
	roomCode, err := session.NewRoomCode()
	if err != nil {
		s.logger.Error("failed to generate room code", "error", err)
		return nil, status.Error(codes.Internal, "room_code was not generated")
	}

	// Build the session
	newSession := &session.Session{
		ID:         sessionID,
		RoomCode:   roomCode,
		CreatedBy:  req.UserId,
		CreatedAt:  time.Now().UTC(),
		MaxCameras: req.MaxCameras,
		Status:     "active",
	}

	// Store the session
	if err := s.store.CreateSession(ctx, newSession); err != nil {
		s.logger.Error("failed to create session", "error", err)
		return nil, status.Error(codes.Internal, "failed to create session")
	}

	// Grant the creator director access
	if err := s.store.GrantAccess(ctx, sessionID, req.UserId, "director"); err != nil {
		s.logger.Error("failed to grant creator access", "error", err)
		return nil, status.Error(codes.Internal, "failed to grant access")
	}

	s.logger.Info(
		"session created",
		"session_id", sessionID,
		"room_code", roomCode,
		"created_by", req.UserId,
	)

	return &pb.CreateSessionReply{
		RoomCode: roomCode,
	}, nil
}

// Sets a session's status to "closed"
// Only the session owner may close it
func (s *Server) CloseSession(ctx context.Context, req *pb.CloseSessionRequest) (*pb.CloseSessionReply, error) {
	// Validate required fields
	if req.RoomCode == "" {
		return nil, status.Error(codes.InvalidArgument, "room_code is required")
	}
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}

	// Retrieves session by room code
	sess, err := s.store.GetSessionByRoomCode(ctx, req.RoomCode)
	if err != nil {
		s.logger.Error("session not found", "room_code", req.RoomCode, "error", err)
		return nil, status.Error(codes.NotFound, "session not found")
	}

	// Verify ownership
	if sess.CreatedBy != req.UserId {
		return nil, status.Error(codes.PermissionDenied, "only the session owner can close it")
	}

	// Update session status to closed
	if err := s.store.UpdateSessionStatus(ctx, sess.ID, "closed"); err != nil {
		s.logger.Error("failed to close session", "error", err)
		return nil, status.Error(codes.Internal, "failed to close session")
	}

	s.logger.Info(
		"session closed",
		"session_id", sess.ID,
		"room_code", req.RoomCode,
		"closed_by", req.UserId,
	)
	return &pb.CloseSessionReply{Success: true}, nil
}

// GetMySessions returns all sessions created by the requesting user
func (s *Server) GetMySessions(ctx context.Context, req *pb.GetMySessionsRequest) (*pb.GetMySessionsReply, error) {
	// Validate required fields
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}

	// Fetch all sessions for this user
	sessions, err := s.store.GetUserSessions(ctx, req.UserId)
	if err != nil {
		s.logger.Error("failed to get user sessions", "user_id", req.UserId, "error", err)
		return nil, status.Error(codes.Internal, "failed to retrieve sessions")
	}

	// Convert session slice to pb.SessionInfo slice
	pbSessions := make([]*pb.SessionInfo, 0, len(sessions))
	for _, sess := range sessions {
		pbSessions = append(pbSessions, &pb.SessionInfo{
			SessionId:  sess.ID,
			RoomCode:   sess.RoomCode,
			CreatedAt:  sess.CreatedAt.Unix(),
			MaxCameras: sess.MaxCameras,
			Status:     sess.Status,
		})
	}
	return &pb.GetMySessionsReply{Sessions: pbSessions}, nil
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
