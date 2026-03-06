package signaling

import (
	"context"

	"github.com/livekit/protocol/livekit"
)

type ingressClient interface {
	CreateIngress(ctx context.Context, req *livekit.CreateIngressRequest) (*livekit.IngressInfo, error)
	DeleteIngress(ctx context.Context, req *livekit.DeleteIngressRequest) (*livekit.IngressInfo, error)
}

