# Dev Environment — GKE Cluster

Terraform configuration for the DirectLink dev GKE cluster.

## Prerequisites

- GCP project `directlink-dev` with billing enabled
- APIs enabled: Kubernetes Engine, Compute Engine, Artifact Registry
- `gcloud` authenticated (`gcloud auth login` + `gcloud auth application-default login`)
- Terraform installed (>= 1.3)

## Usage
```bash
cd infrastructure/terraform/environments/dev

terraform init
terraform plan
terraform apply
```

After provisioning, configure kubectl:
```bash
# Shown in terraform output
gcloud container clusters get-credentials directlink-dev --zone us-south1-a --project directlink-dev
```

Get the node external IP for the Kustomize overlay:
```bash
kubectl get nodes -o wide
```

## Cost Management

This cluster uses **spot VMs** which are ~60-80% cheaper than regular instances.
Even so, a running cluster incurs charges. **Tear down when not testing:**
```bash
terraform destroy
```

To bring it back up:
```bash
terraform apply
```

The node will get a new external IP after re-provisioning. Update the
Kustomize dev overlay's `REPLACE_NODE_IP` values accordingly.

### Estimated Costs (dev cluster)

- GKE management fee: ~$0.10/hour (zonal cluster)
- e2-standard-2 spot node: ~$0.01-0.02/hour
- Total: ~$3-4/day if left running continuously

Set up a budget alert in GCP Billing to avoid surprises.

## Artifact Registry

The Docker image repository is managed separately from this Terraform config
so that `terraform destroy` doesn't delete pushed images. Create it once:
```bash
gcloud artifacts repositories create directlink \
  --repository-format=docker \
  --location=us-south1 \
  --description="Docker images for DirectLink"
```

Image path: `us-south1-docker.pkg.dev/directlink-dev/directlink/signaling`

## What This Provisions

- GKE zonal cluster (us-south1-a, Standard mode)
- Single-node spot VM pool (e2-standard-2, 30GB disk)
- Firewall rules for:
  - TCP 50051 (gRPC signaling)
  - TCP 7880, 7881 (LiveKit WebSocket + RTC)
  - TCP 8080 (WHIP endpoint)
  - UDP 50000–60000 (WebRTC media)

## After Provisioning

1. Configure kubectl with the command from `terraform output`
2. Get node external IP from `kubectl get nodes -o wide`
3. Update Kustomize dev overlay (`infrastructure/kubernetes/overlays/dev/`):
   - Replace `REPLACE_NODE_IP` in three patch files
   - Replace `PROJECT_ID` in `kustomization.yaml`
   - Replace `REPLACE_DEV_SECRET` in three patch files
4. Apply workloads: `kubectl apply -k infrastructure/kubernetes/overlays/dev/`