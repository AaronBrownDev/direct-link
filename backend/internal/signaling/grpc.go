package signaling

import (
	"context"
	"time"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/AaronBrownDev/direct-link/pkg/session"
	"github.com/livekit/protocol/auth"
	"github.com/livekit/protocol/livekit"
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

	// Auto-grant access for valid room code join
	if err := s.store.GrantAccess(ctx, sess.ID, req.UserId, req.Role); err != nil {
		//TODO: handle orphaned session in redis
		s.logger.Error("failed to grant access", "error", err)
		return nil, status.Error(codes.Internal, "failed to grant access")
	}

	switch req.Role {
	case "camera":
		return s.joinAsCamera(ctx, req, sess)
	case "director":
		return s.joinAsDirector(ctx, req, sess)
	default:
		return nil, status.Error(codes.InvalidArgument, "role must be 'camera' or 'director'")
	}
}

func (s *Server) joinAsCamera(ctx context.Context, req *pb.JoinRequest, sess *session.Session) (*pb.JoinReply, error) {

	info, err := s.lkIngressClient.CreateIngress(ctx, &livekit.CreateIngressRequest{
		InputType:           livekit.IngressInput_WHIP_INPUT,
		RoomName:            sess.ID,
		ParticipantIdentity: req.UserId,
		ParticipantName:     req.UserId,
		EnableTranscoding:   func(b bool) *bool { return &b }(false),
	})
	if err != nil {
		s.logger.Error("failed to create ingress", "session_id", sess.ID, "user_id", req.UserId, "error", err)
		return nil, status.Error(codes.Internal, "failed to create ingress")
	}

	if err := s.store.AddIngressID(ctx, sess.ID, info.IngressId); err != nil {
		s.logger.Warn("failed to store ingress ID", "ingress_id", info.IngressId, "error", err)
	}

	s.metrics.TokenGenerationsTotal.WithLabelValues("camera").Inc()
	s.logger.Info("camera joined via WHIP ingress", "session_id", sess.ID, "user_id", req.UserId, "ingress_id", info.IngressId)

	return &pb.JoinReply{
		WhipUrl:   info.Url,
		StreamKey: info.StreamKey,
	}, nil
}

func (s *Server) joinAsDirector(ctx context.Context, req *pb.JoinRequest, sess *session.Session) (*pb.JoinReply, error) {
	canPublish, canSubscribe := false, true

	at := auth.NewAccessToken(s.cfg.LiveKitAPIKey, s.cfg.LiveKitAPISecret)
	grant := &auth.VideoGrant{
		RoomJoin:     true,
		Room:         sess.ID,
		CanPublish:   &canPublish,
		CanSubscribe: &canSubscribe,
	}
	at.SetVideoGrant(grant).SetIdentity(req.UserId).SetValidFor(time.Hour)

	token, err := at.ToJWT()
	if err != nil {
		s.logger.Error("failed to generate token", "error", err)
		return nil, status.Error(codes.Internal, "failed to generate token")
	}

	s.metrics.TokenGenerationsTotal.WithLabelValues("director").Inc()
	s.logger.Info("director joined session", "session_id", sess.ID, "user_id", req.UserId)

	return &pb.JoinReply{
		Token:      token,
		LivekitUrl: s.cfg.LiveKitHost,
	}, nil
}

// CreateSession creates a new production session and returns a room code
func (s *Server) CreateSession(ctx context.Context, req *pb.CreateSessionRequest) (*pb.CreateSessionReply, error) {

	// Validate required fields
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

	// Increment business metrics after successful creation
	s.metrics.SessionsCreatedTotal.Inc()
	s.metrics.SessionsActive.Inc()

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

// CloseSession sets a session's status to "closed"
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

	// Cleanup ingress
	ingressIDs, err := s.store.GetIngressIDs(ctx, sess.ID)
	if err != nil {
		s.logger.Warn("failed to fetch ingress IDs for cleanup", "session_id", sess.ID, "error", err)
	} else {
		for _, id := range ingressIDs {
			_, delErr := s.lkIngressClient.DeleteIngress(ctx, &livekit.DeleteIngressRequest{IngressId: id})
			if delErr != nil {
				s.logger.Warn("failed to delete ingress", "ingress_id", id, "error", delErr)
			} else {
				s.logger.Info("deleted ingress", "ingress_id", id, "session_id", sess.ID)
			}
		}
	}

	// Update session status to closed
	if err := s.store.UpdateSessionStatus(ctx, sess.ID, "closed"); err != nil {
		s.logger.Error("failed to close session", "error", err)
		return nil, status.Error(codes.Internal, "failed to close session")
	}

	// Decrement active sessions
	s.metrics.SessionsActive.Dec()

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
			CreatedAt:  sess.CreatedAt.Format(time.RFC3339),
			MaxCameras: sess.MaxCameras,
			Status:     sess.Status,
		})
	}
	return &pb.GetMySessionsReply{Sessions: pbSessions}, nil
}
