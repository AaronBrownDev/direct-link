package signaling

import (
	"os"
	"time"
)

type Config struct {
	HTTPPort        int
	GRPCPort        int
	ShutdownTimeout time.Duration

	// Redis connection
	RedisAddr         string
	RedisPassword     string
	RedisDB           int
	RedisPoolSize     int
	RedisMinIdle      int
	RedisDialTimeout  time.Duration
	RedisReadTimeout  time.Duration
	RedisWriteTimeout time.Duration
	SessionTTL        time.Duration

	// LiveKit connection
	LiveKitHost        string
	LiveKitExternalURL string
	LiveKitAPIKey      string
	LiveKitAPISecret   string

	// Janitor Cleanup
	JanitorInterval time.Duration
	JanitorLockTTL  time.Duration
}

func DefaultConfig() Config {
	return Config{
		HTTPPort:        8081,
		GRPCPort:        50051,
		ShutdownTimeout: time.Second * 5,

		// Redis Default
		RedisAddr:         "redis:6379",
		RedisPassword:     "",
		RedisDB:           0,
		RedisPoolSize:     10,
		RedisMinIdle:      2,
		RedisDialTimeout:  5 * time.Second,
		RedisReadTimeout:  3 * time.Second,
		RedisWriteTimeout: 3 * time.Second,
		SessionTTL:        24 * time.Hour,

		LiveKitHost:        "http://livekit:7880",
		LiveKitExternalURL: "ws://localhost:7880",
		LiveKitAPIKey:      "devkey",                           // dev default
		LiveKitAPISecret:   "dev-secret-that-is-32-chars-long", // dev default

		JanitorInterval: 1 * time.Minute,
		JanitorLockTTL:  30 * time.Second,
	}
}

func LoadConfig() Config {
	cfg := DefaultConfig()

	if addr := os.Getenv("REDIS_ADDR"); addr != "" {
		cfg.RedisAddr = addr
	}

	if password := os.Getenv("REDIS_PASSWORD"); password != "" {
		cfg.RedisPassword = password
	}

	if host := os.Getenv("LIVEKIT_HOST"); host != "" {
		cfg.LiveKitHost = host
	}

	if url := os.Getenv("LIVEKIT_EXTERNAL_URL"); url != "" {
		cfg.LiveKitExternalURL = url
	}

	if key := os.Getenv("LIVEKIT_API_KEY"); key != "" {
		cfg.LiveKitAPIKey = key
	}

	if secret := os.Getenv("LIVEKIT_API_SECRET"); secret != "" {
		cfg.LiveKitAPISecret = secret
	}

	return cfg
}
