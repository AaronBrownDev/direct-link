package main

import (
	"context"
	"log/slog"
	"os"
	"os/signal"
	"syscall"

	"github.com/AaronBrownDev/direct-link/internal/sfu"
)

func main() {

	logger := slog.New(slog.NewTextHandler(os.Stdout, nil))

	cfg := sfu.DefaultConfig()
	server := sfu.NewServer(cfg, logger)

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	if err := server.ListenAndServe(ctx); err != nil {
		logger.Error("server exited with error", "error", err)
		os.Exit(1)
	}

}
