# Dev Overlay — GKE Deployment

This Kustomize overlay patches the base manifests with GKE dev environment values.
It is **not** for local development (use `docker-compose` for that).

## Prerequisites

- GKE dev cluster provisioned (see `infrastructure/terraform/`)
- `kubectl` configured to target the dev cluster
- Signaling server image pushed to Artifact Registry

## Placeholder Values

Before applying, replace the following placeholders with real values

### `REPLACE_NODE_IP`

The GKE node's external IP address. Find it with:

```bash
kubectl get nodes -o wide
# or
gcloud compute instances list --filter="name~gke"
```

This value appears in three places:

| File                          | Field                  | Purpose                                      |
|-------------------------------|------------------------|----------------------------------------------|
| `patches/configmap.yaml`      | `LIVEKIT_EXTERNAL_URL` | WebSocket URL returned to director clients    |
| `patches/livekit-config.yaml` | `rtc.node_ip`          | IP advertised in WebRTC ICE candidates        |
| `patches/livekit-config.yaml` | `whip_base_url`        | WHIP endpoint URL returned to camera clients  |

**This IP is not static.** If the GKE node restarts (especially preemptible/spot nodes),
update all three values and re-apply.

### `REPLACE_DEV_SECRET`

A LiveKit API secret paired with the `devkey` API key. Generate one or use any
sufficiently long string. This value must match across three files:

| File                          | Field              |
|-------------------------------|--------------------|
| `patches/secret.yaml`         | `LIVEKIT_API_SECRET` |
| `patches/livekit-config.yaml` | `keys.devkey`        |
| `patches/ingress-config.yaml` | `api_secret`         |

### `PROJECT_ID`

Your GCP project ID. Update the `images` block in `kustomization.yaml`.

## Applying

Render and verify before applying:

```bash
# Render to stdout — check for placeholder values and errors
kubectl kustomize infrastructure/kubernetes/overlays/dev/

# Dry run against the cluster
kubectl apply -k infrastructure/kubernetes/overlays/dev/ --dry-run=client

# Apply
kubectl apply -k infrastructure/kubernetes/overlays/dev/
```

## After a Node IP Change

1. Get the new external IP (`kubectl get nodes -o wide`)
2. Update `REPLACE_NODE_IP` in the three locations listed above
3. Re-apply: `kubectl apply -k infrastructure/kubernetes/overlays/dev/`
4. Restart affected pods to pick up ConfigMap changes:

```bash
kubectl rollout restart deployment/signaling -n directlink-dev
kubectl rollout restart deployment/livekit -n directlink-dev
kubectl rollout restart deployment/ingress -n directlink-dev
```