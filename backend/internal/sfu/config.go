package sfu

import (
	"time"
)

type Config struct {
	HTTPPort        int
	GRPCPort        int
	ShutdownTimeout time.Duration
	// TODO: SFUConfig will be added later when integrating ion-sfu
}

func DefaultConfig() Config {
	return Config{
		// differing ports to avoid conflicts with local testing
		// TODO: create a docker-compose.yml for local testing to avoid this
		HTTPPort:        8082,
		GRPCPort:        50052,
		ShutdownTimeout: time.Second * 5,
	}
}
