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

	sessionID := session.NewSessionID()
	roomCode, err := session.NewRoomCode()
	if err != nil {
		s.logger.Error("failed to generate room code", "error", err)
	}

	newSession := &session.Session{
		ID:         sessionID,
		RoomCode:   roomCode,
		CreatedBy:  req.UserId,
		CreatedAt:  time.Now().UTC(),
		MaxCameras: int32(req.MaxCameras),
		Status:     "active",
	}

	if s.store != nil {
		if err := s.store.CreateSession(ctx, newSession); err != nil {
			s.logger.Error("failed to create session", "error", err)
			return nil, status.Error(codes.Internal, "failed to create session")
		}

		if err := s.store.GrantAccess(ctx, sessionID, req.UserId, "director"); err != nil {
			s.logger.Error("failed to grant creator access", "error", err)
		}
	}

	s.logger.Info(
		"session created",
		"session_id", sessionID,
		"room_code", roomCode,
		"created_by", req.UserId,
	)

	return &pb.CreateSessionReply{
		SessionId: sessionID,
		RoomCode:  roomCode,
	}, nil
}

// Sets a session's status to "closed"
// Only the session owner may close it
func (s *Server) CloseSession(ctx context.Context, req *pb.CloseSessionRequest) (*pb.CloseSessionReply, error) {
	if req.SessionId == "" {
		return nil, status.Error(codes.InvalidArgument, "session_id if required")
	}
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}

	if s.store != nil {
		sess, err := s.store.GetSession(ctx, req.SessionId)
		if err != nil {
			s.logger.Error("session not found", "session_id", req.SessionId, "error", err)
			return nil, status.Error(codes.NotFound, "session not found")
		}

		if sess.CreatedBy != req.UserId {
			return nil, status.Error(codes.PermissionDenied, "only the session owner can close it")
		}

		if sess.Status == "closed" {
			return &pb.CloseSessionReply{Success: true}, nil
		}

		if err := s.store.UpdateSessionStatus(ctx, req.SessionId, "closed"); err != nil {
			s.logger.Error("failed to close session", "error", err)
			return nil, status.Error(codes.Internal, "failed to close session")
		}
	}

	s.logger.Info(
		"session closed",
		"session_id", req.SessionId,
		"closed_by", req.UserId,
	)
	return &pb.CloseSessionReply{Success: true}, nil
}

// Returns all sessions created by the requesting user
func (s *Server) GetMySessions(ctx context.Context, req *pb.GetMySessionRequest) (*pb.GetMySessionReply, error) {
	if req.UserId == "" {
		return nil, status.Error(codes.InvalidArgument, "user_id is required")
	}

	if s.store == nil {
		return &pb.GetMySessionReply{Sessions: nil}, nil
	}

	sessions, err := s.store.GetUserSessions(ctx, req.UserId)
	if err != nil {
		s.logger.Error("failed to get user sessions", "user_id", req.UserId, "error", err)
		return nil, status.Error(codes.Internal, "failed to retrieved sessions")
	}
	infos := make([]*pb.SessionInfo, 0, len(sessions))
	for _, sess := range sessions {
		infos = append(infos, &pb.SessionInfo{
			SessionId:  sess.ID,
			RoomCode:   sess.RoomCode,
			CreatedAt:  sess.CreatedAt.Unix(),
			MaxCameras: int32(sess.MaxCameras),
			Status:     sess.Status,
		})
	}
	return &pb.GetMySessionReply{Sessions: infos}, nil
}
