package signaling

import (
	"time"

	"github.com/BurntSushi/toml"
	ionsfu "github.com/pion/ion-sfu/pkg/sfu"
)

type Config struct {
	HTTPPort        int
	GRPCPort        int
	ShutdownTimeout time.Duration
	SFU             ionsfu.Config
}

func DefaultConfig() Config {
	return Config{
		// differing ports to avoid conflicts with local testing
		// TODO: create a docker-compose.yml for local testing to avoid this
		HTTPPort:        8081,
		GRPCPort:        50051,
		ShutdownTimeout: time.Second * 5,
		SFU:             ionsfu.Config{}, // Gets overridden by TOML
	}
}

func LoadConfig(path string) (*Config, error) {
	cfg := DefaultConfig()

	if _, err := toml.Decode(path, &cfg); err != nil {
		return nil, err
	}

	return &cfg, nil
}
