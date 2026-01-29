package signaling

import "time"

type Config struct {
	HTTPPort        int
	GRPCPort        int
	ShutdownTimeout time.Duration
}

func DefaultConfig() Config {
	return Config{
		// differing ports to avoid conflicts with local testing
		// TODO: create a docker-compose.yml for local testing to avoid this
		HTTPPort:        8081,
		GRPCPort:        50051,
		ShutdownTimeout: time.Second * 5,
	}
}
