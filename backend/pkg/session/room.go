package session

import (
	"crypto/rand"
	"fmt"
	"math/big"

	"github.com/google/uuid"
)

// Creates a unique Session identifier
func NewSessionID() string {
	return uuid.New().String()
}

// Created a human readable room code
func NewRoomCode() (string, error) {
	n, _ := rand.Int(rand.Reader, big.NewInt(10000))
	return fmt.Sprintf("ROOM-%04d", n.Int64()), nil
}
