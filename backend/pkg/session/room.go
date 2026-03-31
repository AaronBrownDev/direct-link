package session

import (
	"context"
	"crypto/rand"
	"errors"
	"fmt"
	"math/big"

	"github.com/google/uuid"
)

const maxRoomAttempts = 10

// Creates a unique Session identifier
func NewSessionID() string {
	return uuid.New().String()
}

// Creates human readable room code
func NewRoomCode(ctx context.Context, store Store) (string, error) {
	for range maxRoomAttempts {
		n, err := rand.Int(rand.Reader, big.NewInt(1_00_00_00)) // six digits
		code := fmt.Sprintf("ROOM-%06d", n.Int64())
		if err != nil {
			return "", err
		}

		// Check if the generated room code is unique
		_, err = store.GetSessionByRoomCode(ctx, code)
		if errors.Is(err, ErrInvalidRoomCode) {
			return code, nil
		}
		if err != nil {
			return "", err
		}

	}
	return "", fmt.Errorf("failed to generate unique room code after %d attempts", maxRoomAttempts)
}
