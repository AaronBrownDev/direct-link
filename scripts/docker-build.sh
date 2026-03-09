#!/usr/bin/env bash
# Build the signaling server Docker image.
# Usage: ./scripts/docker-build.sh [tag]

set -euo pipefail

TAG="${1:-dev}"
IMAGE="directlink/signaling:${TAG}"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")"

echo "Building ${IMAGE} (commit: ${COMMIT})"

DOCKER_BUILDKIT=1 docker build \
  -f backend/Dockerfile \
  -t "${IMAGE}" \
  --build-arg "VERSION=${TAG}" \
  --build-arg "COMMIT=${COMMIT}" \
  .

echo ""
docker image ls "${IMAGE}"