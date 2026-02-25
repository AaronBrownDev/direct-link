package signaling

import (
	"context"
	"time"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Creates a new production session and returns a room code
func (s *Server) CreateSession(ctx context.Context, req *pb.CreateSessionRequest) (*pb.CreateSessionReply, error) {
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}
	if req.MaxCameras <= 0 {
		return nil, status.Error(codes.InvalidArgument, ",max_cameras must be greater than zero")
	}

	// Generate session ID and room code
	sessionID := session.NewSessionID()
	roomCode, err := session.NewRoomCode()
	if err != nil {
		s.logger.Error("failed to generate room code", "error", err)
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
	return &pb.CloseSessionReply{}, nil
}

// GetMySessions returns all sessions created by the requesting user
func (s *Server) GetMySessions(ctx context.Context, req *pb.GetMySessionsRequest) (*pb.GetMySessionsReply, error) {
	//Validate required fields
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}

	// Fetch all sessions for this user
	sessions, err := s.store.GetUserSessions(ctx, req.UserId)
	if err != nil {
		s.logger.Error("failed to get user sessions", "user_id", req.UserId, "error", err)
		return nil, status.Error(codes.Internal, "failed to retrieved sessions")
	}

	// Convert session slice to pb.SessioInfo slice
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
