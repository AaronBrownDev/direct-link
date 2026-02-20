package signaling

import (
	"crypto/rand"
	"fmt"
	"math/big"

	"github.com/google/uuid"
)

// Creates a unique Session identifier
func GenerateSessionID() string {
	return uuid.New().String()
}

// Created a human readable room code
func GenerateRoomCode() string {
	n, _ := rand.Int(rand.Reader, big.NewInt(10000))
	return fmt.Sprintf("ROOM-%04d", n.Int64())
}
