package main

import (
	"context"
	"log/slog"
	"os"
	"os/signal"
	"syscall"

	"github.com/AaronBrownDev/direct-link/internal/janitor"
	"github.com/AaronBrownDev/direct-link/internal/signaling"
	"github.com/redis/go-redis/v9"
)

func main() {
	os.Exit(run())
}

func run() int {

	logger := slog.New(slog.NewTextHandler(os.Stdout, nil))

	cfg := signaling.LoadConfig()
	server := signaling.NewServer(cfg, logger)

	lockClient := redis.NewClient(&redis.Options{
		Addr:     cfg.RedisAddr,
		Password: cfg.RedisPassword,
		DB:       cfg.RedisDB,
	})

	j := janitor.New(
		server.Store(),
		lockClient,
		server.DeleteRoom,
		logger,
		cfg.JanitorInterval,
		cfg.JanitorLockTTL,
		server.Metrics(),
	)
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	// Starts Janitor as a standalone goroutine alongside the server.
	// It stops cleanly when ctx is cancelled on shutdown
	go j.Run(ctx)

	if err := server.ListenAndServe(ctx); err != nil {
		logger.Error("server exited with error", "error", err)
		return 1
	}

	// Close the janitor's dedicated lock client on shutdown
	if err := lockClient.Close(); err != nil {
		logger.Error("failed to close janitor lock client", "error", err)
	}
	return 0
}
