package unit

import (
	"strings"
	"testing"

	"github.com/AaronBrownDev/direct-link/internal/signaling"
)

// Generate Session ID
func TestGenerateSessionID_isNonEmpty(t *testing.T) {
	id := signaling.GenerateSessionID()
	if id == "" {
		t.Fatal("generateSessionID method returned empty string")
	}

}

// Tests for sessionID format
func TestGenerateSessionID_isUUID(t *testing.T) {
	id := signaling.GenerateSessionID()
	parts := strings.Split(id, "-")
	if len(parts) != 5 {
		t.Errorf("expected 5 UUID parts, got %d (id=%q)", len(parts), id)
	}
	lengths := []int{8, 4, 4, 4, 12}
	for i, p := range parts {
		if len(p) != lengths[i] {
			t.Errorf("UUID part %d: expected len %d, got %d", i, lengths[i], len(p))
		}
	}
}

// Tests for duplicate session id
func TestGenerateSessionID_UniqueAcrossCalls(t *testing.T) {
	seen := make(map[string]struct{}, 100)
	for i := range 100 {
		id := signaling.GenerateSessionID()
		if _, dup := seen[id]; dup {
			t.Fatalf("duplicate session ID on iteration %d: %q", i, id)
		}
		seen[id] = struct{}{}
	}
}

// Tests roomcode dormat
func TestGenerateRoomCode_Format(t *testing.T) {
	code := signaling.GenerateRoomCode()

	if !strings.HasPrefix(code, "ROOM-") {
		t.Errorf("expected ROOM-prefix, got %q", code)
	}

	suffix := strings.TrimPrefix(code, "ROOM-")
	if len(suffix) != 4 {
		t.Errorf("expected 4-digit suffix, got %q (len=%d)", suffix, len(suffix))
	}

	for _, ch := range suffix {
		if ch < '0' || ch > '9' {
			t.Errorf("non-digit character %q in suffix %q", ch, suffix)
		}
	}
}

// Tests the duplicate room codes
func TestGenerateRoomCode_UniqueAcrossCalls(t *testing.T) {
	seen := make(map[string]struct{}, 200)
	collisions := 0
	for range 200 {
		code := signaling.GenerateRoomCode()
		if _, dup := seen[code]; dup {
			collisions++
		}
		seen[code] = struct{}{}
	}
	if collisions > 5 {
		t.Errorf("too many duplicate room codes: %d / 200", collisions)
	}
}
