# Architecture Documentation

Technical architecture documentation for DirectLink.

| Document | Description |
|----------|-------------|
| [System Overview](system-overview.md) | Top-level architecture, component descriptions, data flow, technology choices, and repository layout. Start here. |
| [Signaling Protocol](signaling-protocol.md) | gRPC API contract, session lifecycle, role-based credential distribution, Redis data model, error handling, and observability. |
| [Video Pipeline](video-pipeline.md) | End-to-end media path from camera capture through NVENC encoding, WHIP ingest, LiveKit SFU routing, NVDEC decode, and Qt rendering. Includes latency budget. |
| [Infrastructure](infrastructure.md) | Kubernetes topology, Kustomize base/overlay strategy, GKE Terraform provisioning, CI/CD pipelines, container strategy, and observability stack. |