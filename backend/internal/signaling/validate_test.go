package signaling

import (
	"strings"
	"testing"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// assertInvalidArgument is a helper that checks the error is a gRPC
// InvalidArgument status containing the expected message substring.
func assertInvalidArgument(t *testing.T, err error, msgSubstring string) {
	t.Helper()
	if err == nil {
		t.Fatal("expected error, got nil")
	}
	st, ok := status.FromError(err)
	if !ok {
		t.Fatalf("expected gRPC status error, got %T: %v", err, err)
	}
	if st.Code() != codes.InvalidArgument {
		t.Errorf("expected code InvalidArgument, got %s", st.Code())
	}
	if !strings.Contains(st.Message(), msgSubstring) {
		t.Errorf("expected message to contain %q, got %q", msgSubstring, st.Message())
	}
}

// --- validateRoomCode ---

func TestValidateRoomCode(t *testing.T) {
	tests := []struct {
		name       string
		input      string
		wantErr    bool
		wantSubstr string
	}{
		{
			name:    "valid room code",
			input:   "ROOM-000472",
			wantErr: false,
		},
		{
			name:       "empty room code",
			input:      "",
			wantErr:    true,
			wantSubstr: "room_code is required",
		},
		{
			name:       "missing ROOM prefix",
			input:      "000472",
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
		{
			name:       "too few digits",
			input:      "ROOM-123",
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
		{
			name:       "too many digits",
			input:      "ROOM-1234567",
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
		{
			name:       "letters in digit section",
			input:      "ROOM-ABCDEF",
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
		{
			name:       "lowercase prefix",
			input:      "room-000472",
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
		{
			name:       "missing hyphen",
			input:      "ROOM000472",
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateRoomCode(tt.input)
			if tt.wantErr {
				assertInvalidArgument(t, err, tt.wantSubstr)
			} else if err != nil {
				t.Errorf("expected no error, got %v", err)
			}
		})
	}
}

// --- validateUserID ---

func TestValidateUserID(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		wantErr bool
	}{
		{
			name:    "valid short id",
			input:   "director-1",
			wantErr: false,
		},
		{
			name:    "valid 64 character id",
			input:   strings.Repeat("a", 64),
			wantErr: false,
		},
		{
			name:    "empty id",
			input:   "",
			wantErr: true,
		},
		{
			name:    "65 character id",
			input:   strings.Repeat("a", 65),
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateUserID(tt.input)
			if tt.wantErr {
				assertInvalidArgument(t, err, "user_id must be between 1 and 64 characters")
			} else if err != nil {
				t.Errorf("expected no error, got %v", err)
			}
		})
	}
}

// --- validateJoinRequest ---

func TestValidateJoinRequest(t *testing.T) {
	tests := []struct {
		name       string
		req        *pb.JoinRequest
		wantErr    bool
		wantSubstr string
	}{
		{
			name:    "valid camera request",
			req:     &pb.JoinRequest{RoomCode: "ROOM-000472", UserId: "camera-1", Role: "camera"},
			wantErr: false,
		},
		{
			name:    "valid director request",
			req:     &pb.JoinRequest{RoomCode: "ROOM-000472", UserId: "director-1", Role: "director"},
			wantErr: false,
		},
		{
			name:       "empty room code",
			req:        &pb.JoinRequest{RoomCode: "", UserId: "camera-1", Role: "camera"},
			wantErr:    true,
			wantSubstr: "room_code is required",
		},
		{
			name:       "malformed room code",
			req:        &pb.JoinRequest{RoomCode: "ROOM-ABC", UserId: "camera-1", Role: "camera"},
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
		{
			name:       "empty user id",
			req:        &pb.JoinRequest{RoomCode: "ROOM-000472", UserId: "", Role: "camera"},
			wantErr:    true,
			wantSubstr: "user_id must be between 1 and 64 characters",
		},
		{
			name:       "user id too long",
			req:        &pb.JoinRequest{RoomCode: "ROOM-000472", UserId: strings.Repeat("a", 65), Role: "camera"},
			wantErr:    true,
			wantSubstr: "user_id must be between 1 and 64 characters",
		},
		{
			name:       "invalid role",
			req:        &pb.JoinRequest{RoomCode: "ROOM-000472", UserId: "user-1", Role: "admin"},
			wantErr:    true,
			wantSubstr: "role must be 'camera' or 'director'",
		},
		{
			name:       "empty role",
			req:        &pb.JoinRequest{RoomCode: "ROOM-000472", UserId: "user-1", Role: ""},
			wantErr:    true,
			wantSubstr: "role must be 'camera' or 'director'",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateJoinRequest(tt.req)
			if tt.wantErr {
				assertInvalidArgument(t, err, tt.wantSubstr)
			} else if err != nil {
				t.Errorf("expected no error, got %v", err)
			}
		})
	}
}

// --- validateCreateSessionRequest ---

func TestValidateCreateSessionRequest(t *testing.T) {
	tests := []struct {
		name       string
		req        *pb.CreateSessionRequest
		wantErr    bool
		wantSubstr string
	}{
		{
			name:    "valid request",
			req:     &pb.CreateSessionRequest{UserId: "director-1", MaxCameras: 4},
			wantErr: false,
		},
		{
			name:    "min cameras",
			req:     &pb.CreateSessionRequest{UserId: "director-1", MaxCameras: 1},
			wantErr: false,
		},
		{
			name:       "empty user id",
			req:        &pb.CreateSessionRequest{UserId: "", MaxCameras: 4},
			wantErr:    true,
			wantSubstr: "user_id must be between 1 and 64 characters",
		},
		{
			name:       "user id too long",
			req:        &pb.CreateSessionRequest{UserId: strings.Repeat("a", 65), MaxCameras: 4},
			wantErr:    true,
			wantSubstr: "user_id must be between 1 and 64 characters",
		},
		{
			name:       "zero cameras",
			req:        &pb.CreateSessionRequest{UserId: "director-1", MaxCameras: 0},
			wantErr:    true,
			wantSubstr: "max_cameras must be between 1 and 4",
		},
		{
			name:       "too many cameras",
			req:        &pb.CreateSessionRequest{UserId: "director-1", MaxCameras: 5},
			wantErr:    true,
			wantSubstr: "max_cameras must be between 1 and 4",
		},
		{
			name:       "negative cameras",
			req:        &pb.CreateSessionRequest{UserId: "director-1", MaxCameras: -1},
			wantErr:    true,
			wantSubstr: "max_cameras must be between 1 and 4",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateCreateSessionRequest(tt.req)
			if tt.wantErr {
				assertInvalidArgument(t, err, tt.wantSubstr)
			} else if err != nil {
				t.Errorf("expected no error, got %v", err)
			}
		})
	}
}

// --- validateCloseSessionRequest ---

func TestValidateCloseSessionRequest(t *testing.T) {
	tests := []struct {
		name       string
		req        *pb.CloseSessionRequest
		wantErr    bool
		wantSubstr string
	}{
		{
			name:    "valid request",
			req:     &pb.CloseSessionRequest{RoomCode: "ROOM-000472", UserId: "director-1"},
			wantErr: false,
		},
		{
			name:       "empty room code",
			req:        &pb.CloseSessionRequest{RoomCode: "", UserId: "director-1"},
			wantErr:    true,
			wantSubstr: "room_code is required",
		},
		{
			name:       "malformed room code",
			req:        &pb.CloseSessionRequest{RoomCode: "ROOM-ABC", UserId: "director-1"},
			wantErr:    true,
			wantSubstr: "room_code must match format ROOM-XXXXXX",
		},
		{
			name:       "empty user id",
			req:        &pb.CloseSessionRequest{RoomCode: "ROOM-000472", UserId: ""},
			wantErr:    true,
			wantSubstr: "user_id must be between 1 and 64 characters",
		},
		{
			name:       "user id too long",
			req:        &pb.CloseSessionRequest{RoomCode: "ROOM-000472", UserId: strings.Repeat("a", 65)},
			wantErr:    true,
			wantSubstr: "user_id must be between 1 and 64 characters",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateCloseSessionRequest(tt.req)
			if tt.wantErr {
				assertInvalidArgument(t, err, tt.wantSubstr)
			} else if err != nil {
				t.Errorf("expected no error, got %v", err)
			}
		})
	}
}

// --- validateGetMySessionsRequest ---

func TestValidateGetMySessionsRequest(t *testing.T) {
	tests := []struct {
		name    string
		req     *pb.GetMySessionsRequest
		wantErr bool
	}{
		{
			name:    "valid request",
			req:     &pb.GetMySessionsRequest{UserId: "director-1"},
			wantErr: false,
		},
		{
			name:    "empty user id",
			req:     &pb.GetMySessionsRequest{UserId: ""},
			wantErr: true,
		},
		{
			name:    "user id too long",
			req:     &pb.GetMySessionsRequest{UserId: strings.Repeat("a", 65)},
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateGetMySessionsRequest(tt.req)
			if tt.wantErr {
				assertInvalidArgument(t, err, "user_id must be between 1 and 64 characters")
			} else if err != nil {
				t.Errorf("expected no error, got %v", err)
			}
		})
	}
}
