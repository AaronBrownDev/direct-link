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

var roomCodeRegex = regexp.MustCompile(`^ROOM-\d{6}$`)

func validateRoomCode(code string) error {
	if code == "" {
		return status.Error(codes.InvalidArgument, "room_code is required")
	}
	if !roomCodeRegex.MatchString(code) {
		return status.Error(codes.InvalidArgument, "room_code must match format ROOM-XXXXXX")
	}

	return nil
}

func validateUserID(id string) error {
	if len(id) == 0 || len(id) > maxUserIDLength {
		return status.Error(codes.InvalidArgument, "user_id must be between 1 and 64 characters")
	}
	return nil
}

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

func validateCreateSessionRequest(req *pb.CreateSessionRequest) error {
	if err := validateUserID(req.UserId); err != nil {
		return err
	}
	if req.MaxCameras <= 0 || req.MaxCameras > maxCamerasAllowed {
		return status.Error(codes.InvalidArgument, "max_cameras must be between 1 and 4")
	}
	return nil
}

func validateCloseSessionRequest(req *pb.CloseSessionRequest) error {
	if err := validateRoomCode(req.RoomCode); err != nil {
		return err
	}
	if err := validateUserID(req.UserId); err != nil {
		return err
	}
	return nil
}

func validateGetMySessionsRequest(req *pb.GetMySessionsRequest) error {
	if err := validateUserID(req.UserId); err != nil {
		return err
	}
	return nil
}
