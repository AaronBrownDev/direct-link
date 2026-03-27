# Dev Overlay — GKE Deployment

This Kustomize overlay patches the base manifests with GKE dev environment values.
It is **not** for local development (use `docker-compose` for that).

## Prerequisites

- GKE dev cluster provisioned (see `infrastructure/terraform/`)
- `kubectl` configured to target the dev cluster
- Signaling server image pushed to Artifact Registry
- External Secrets Operator installed in the cluster
- GCP Secret Manager secrets created (`directlink-livekit-api-key`, `directlink-livekit-api-secret`)
- Workload Identity binding configured for the ESO service account
- Static IP addresses reserved for LoadBalancer services:

```bash
gcloud compute addresses create directlink-dev-signaling \
  --project directlink-dev --region us-south1

gcloud compute addresses create directlink-dev-livekit \
  --project directlink-dev --region us-south1

gcloud compute addresses create directlink-dev-ingress \
  --project directlink-dev --region us-south1
```

## Hardcoded IPs

The following files contain hardcoded static LoadBalancer IPs. If a static IP
reservation is released and re-created (new address), update these values:

| File                          | Field                  | IP Source                    | Purpose                                      |
|-------------------------------|------------------------|------------------------------|----------------------------------------------|
| `patches/configmap.yaml`      | `LIVEKIT_EXTERNAL_URL` | `directlink-dev-livekit`     | WebSocket URL returned to director clients    |
| `patches/livekit-config.yaml` | `rtc.node_ip`          | `directlink-dev-livekit`     | IP advertised in WebRTC ICE candidates        |
| `patches/livekit-config.yaml` | `whip_base_url`        | `directlink-dev-ingress`     | WHIP endpoint URL returned to camera clients  |

To look up the current addresses:

```bash
gcloud compute addresses list --project directlink-dev --filter="name~directlink-dev"
```

## Secrets (GCP Secret Manager)

Secrets are managed by the External Secrets Operator (ESO), which syncs them from
GCP Secret Manager into a Kubernetes Secret (`directlink-secret`). The following
secrets must exist in the `directlink-dev` GCP project:

| Secret Manager Name                | K8s Secret Key       | Purpose                          |
|------------------------------------|----------------------|----------------------------------|
| `directlink-livekit-api-key`       | `LIVEKIT_API_KEY`    | LiveKit API key                  |
| `directlink-livekit-api-secret`    | `LIVEKIT_API_SECRET` | LiveKit API secret               |

ESO refreshes secrets every hour. To force an immediate sync:

```bash
kubectl annotate externalsecret directlink-secret -n directlink-dev \
  force-sync=$(date +%s) --overwrite
```

## Applying

Render and verify before applying:

```bash
# Render to stdout — check for errors
kubectl kustomize infrastructure/kubernetes/overlays/dev/

# Dry run against the cluster
kubectl apply -k infrastructure/kubernetes/overlays/dev/ --dry-run=client

# Apply
kubectl apply -k infrastructure/kubernetes/overlays/dev/
```

## After a Static IP Change

1. Look up the new addresses: `gcloud compute addresses list --project directlink-dev --filter="name~directlink-dev"`
2. Update the three locations listed in the Hardcoded IPs table above
3. Re-apply: `kubectl apply -k infrastructure/kubernetes/overlays/dev/`
4. Restart affected pods to pick up ConfigMap changes:

```bash
kubectl rollout restart deployment/signaling -n directlink-dev
kubectl rollout restart deployment/livekit -n directlink-dev
kubectl rollout restart deployment/ingress -n directlink-dev
```