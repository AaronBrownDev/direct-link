package signaling

import (
	"regexp"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

const (
	maxUserIDLength   = 64
	maxCamerasAllowed = 4
)

// roomCodeRegex is compiled once at package init for performance
// Valid format ROOM-XXXXXX where X is a digit (e.g. ROOM-XXXXXX)
var roomCodeRegex = regexp.MustCompile(`^ROOM-\d{6}$`)

// validateRoomCode is a shared helper used by any request that carries a room code.
// Empty and malformed inputs produce distinct error messages
func validateRoomCode(code string) error {
	if code == "" {
		return status.Error(codes.InvalidArgument, "room_code is required")
	}
	if !roomCodeRegex.MatchString(code) {
		return status.Error(codes.InvalidArgument, "room_code must match format ROOM-XXXXXX")
	}

	return nil
}

// validateUserID is a shared helper used by every request that carries a user_id.
func validateUserID(id string) error {
	if len(id) == 0 || len(id) > maxUserIDLength {
		return status.Error(codes.InvalidArgument, "user_id must be between 1 and 64 characters")
	}
	return nil
}

// validateJoinRequest validates a JoinSession request
func validateJoinRequest(req *pb.JoinRequest) error {
	if err := validateRoomCode(req.RoomCode); err != nil {
		return err
	}
	if err := validateUserID(req.UserId); err != nil {
		return err
	}

	if req.Role != "camera" && req.Role != "director" {
		return status.Error(codes.InvalidArgument, "role must be 'camera' or 'director'")
	}
	return nil
}

// validateCreateSessionRequest validates a CreateSession request
func validateCreateSessionRequest(req *pb.CreateSessionRequest) error {
	if err := validateUserID(req.UserId); err != nil {
		return err
	}
	if req.MaxCameras <= 0 || req.MaxCameras > maxCamerasAllowed {
		return status.Error(codes.InvalidArgument, "max_cameras must be between 1 and 4")
	}
	return nil
}

// validateCloseSessionRequest validates a CloseSession request
func validateCloseSessionRequest(req *pb.CloseSessionRequest) error {
	if err := validateRoomCode(req.RoomCode); err != nil {
		return err
	}
	if err := validateUserID(req.UserId); err != nil {
		return err
	}
	return nil
}

// validateGetMySessionsRequest validates a GetMySessions request
func validateGetMySessionsRequest(req *pb.GetMySessionsRequest) error {
	if err := validateUserID(req.UserId); err != nil {
		return err
	}
	return nil
}
