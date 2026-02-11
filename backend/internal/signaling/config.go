package signaling

import (
	"os"
	"time"
)

type Config struct {
	HTTPPort        int
	GRPCPort        int
	ShutdownTimeout time.Duration

	// LiveKit connection
	LiveKitHost      string
	LiveKitAPIKey    string
	LiveKitAPISecret string
}

func DefaultConfig() Config {
	return Config{
		HTTPPort:         8081,
		GRPCPort:         50051,
		ShutdownTimeout:  time.Second * 5,
		LiveKitHost:      "http://localhost:7880",
		LiveKitAPIKey:    "devkey", // dev default
		LiveKitAPISecret: "secret", // dev default
	}
}

func LoadConfig() Config {
	cfg := DefaultConfig()

	if host := os.Getenv("LIVEKIT_HOST"); host != "" {
		cfg.LiveKitHost = host
	}

	if key := os.Getenv("LIVEKIT_API_KEY"); key != "" {
		cfg.LiveKitAPIKey = key
	}

	if secret := os.Getenv("LIVEKIT_API_SECRET"); secret != "" {
		cfg.LiveKitAPISecret = secret
	}

	return cfg
}
