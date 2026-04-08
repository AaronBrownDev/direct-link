package signaling

import (
	"os"
	"strconv"
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
	RedisMaxRetries   int
	RedisRetryBackoff time.Duration
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
		RedisMaxRetries:   3,
		RedisRetryBackoff: 100 * time.Millisecond,
		SessionTTL:        24 * time.Hour,

		LiveKitHost:        "http://livekit:7880",
		LiveKitExternalURL: "ws://localhost:7880",
		LiveKitAPIKey:      "devkey",                           // dev default
		LiveKitAPISecret:   "dev-secret-that-is-32-chars-long", // dev default

		JanitorInterval: 1 * time.Minute,
		JanitorLockTTL:  5 * time.Minute,
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

	if interval := os.Getenv("JANITOR_INTERVAL"); interval != "" {
		if d, err := time.ParseDuration(interval); err == nil {
			cfg.JanitorInterval = d
		}
	}

	if lockTTL := os.Getenv("JANITOR_LOCK_TTL"); lockTTL != "" {
		if d, err := time.ParseDuration(lockTTL); err == nil {
			cfg.JanitorLockTTL = d
		}
	}

	if v := os.Getenv("REDIS_MAX_RETRIES"); v != "" {
		if n, err := strconv.Atoi(v); err == nil && n >= 0 {
			cfg.RedisMaxRetries = n
		}
	}

	if v := os.Getenv("REDIS_RETRY_BACKOFF"); v != "" {
		if d, err := time.ParseDuration(v); err == nil && d > 0 {
			cfg.RedisRetryBackoff = d
		}
	}

	return cfg
}
