package main

import (
	"context"
	"log/slog"
	"os"
	"os/signal"
	"syscall"

	"github.com/AaronBrownDev/direct-link/internal/signaling"
)

func main() {
	os.Exit(run())
}

func run() int {

	logger := slog.New(slog.NewTextHandler(os.Stdout, nil))

	cfg := signaling.LoadConfig()
	server := signaling.NewServer(cfg, logger)

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	if err := server.ListenAndServe(ctx); err != nil {
		logger.Error("server exited with error", "error", err)
		return 1
	}

	return 0
}
