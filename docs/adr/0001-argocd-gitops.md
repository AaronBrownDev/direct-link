# 0001. ArgoCD GitOps for DirectLink Continuous Delivery

## Status

Proposed

## Context

### Problem

DirectLink has CI pipelines (`backend-ci.yml`, `video-core-ci.yml`) that build and test code on push to `main`, but no continuous delivery step. Deploying to the GKE dev cluster (`directlink-dev` in `us-south1-a`) is a manual process: build the image locally, push to Artifact Registry, and `kubectl apply -k` the overlays. This is error-prone, unreproducible, and does not scale to staging or production environments.

### Current state of the codebase

**Kubernetes manifests** follow a Kustomize base + overlays pattern at `infrastructure/kubernetes/`:

- **Base** (`infrastructure/kubernetes/base/kustomization.yaml`): defines the full stack -- signaling server, Redis (with PVC), LiveKit SFU, and LiveKit Ingress. Uses common labels (`app.kubernetes.io/part-of: directlink`, `app.kubernetes.io/managed-by: kustomize`) and a `directlink` namespace.
- **Dev overlay** (`infrastructure/kubernetes/overlays/dev/kustomization.yaml`): overrides namespace to `directlink-dev`, patches config/services for the dev environment, includes ESO resources (`cluster-secret-store.yaml`, `external-secret.yaml`), and sets the signaling image to `us-south1-docker.pkg.dev/directlink-dev/directlink/signaling:latest`.
- **Staging and prod overlays** exist as empty directories (`infrastructure/kubernetes/overlays/staging/`, `infrastructure/kubernetes/overlays/prod/`).

**Image build**: The signaling server has a multi-stage Dockerfile at `backend/Dockerfile` (Go build into distroless, with `VERSION` and `COMMIT` build args). The base image reference is `directlink/signaling:dev`, overridden per overlay via `images:` in the kustomization.

**Secrets**: External Secrets Operator is deployed, using a `ClusterSecretStore` backed by GCP Secret Manager via Workload Identity (`directlink-dev.svc.id.goog`). Secrets (LiveKit API key/secret) are synced into a `directlink-secret` Kubernetes Secret.

**Terraform**: Local backend (`backend "local" {}`), single `dev` environment at `infrastructure/terraform/environments/dev/`, using modules for `gke-cluster`, `monitoring`, and `networking`. The GKE cluster has Workload Identity enabled (`workload_pool = "${var.project_id}.svc.id.goog"`). Spot VMs, single node, `e2-standard-2`.

**CI workflows**: `backend-ci.yml` triggers on pushes to `main` affecting `backend/**` or `shared/protocol/**`. It builds, vets, tests (with race detection against a real Redis), and lints. `video-core-ci.yml` does the same for the C++ video-core. Neither workflow builds container images or deploys anything.

### Design constraints

- **Monorepo**: application code, Kubernetes manifests, and Terraform all live in one repository. A separate GitOps config repo would add coordination overhead for a small team.
- **Kustomize-native**: the existing manifests use Kustomize. Introducing Helm for ArgoCD itself or for the application would be a second packaging system with no clear benefit.
- **ESO for secrets**: already wired up and working. Sealed Secrets or SOPS would be redundant.
- **Small team, limited environments**: dev/staging/prod. The system should be simple to operate, not enterprise-grade complex.

## Decision

We will deploy ArgoCD to the GKE dev cluster using a Kustomize-based installation, adopt the App-of-Apps pattern for managing per-environment applications, and extend the existing CI workflows to build/push images and update image tags in the repository. ArgoCD will then detect the tag change and sync.

### 1. ArgoCD installation via Kustomize

ArgoCD will be installed as a Kustomize overlay pinned to a specific release, stored alongside the existing infrastructure manifests.

**Directory structure:**

```
infrastructure/kubernetes/argocd/
  base/
    kustomization.yaml          # references upstream install manifest
  overlays/
    dev/
      kustomization.yaml        # patches for dev (ingress, RBAC, etc.)
      argocd-cm-patch.yaml      # ConfigMap patch: repo URL, kustomize build opts
      argocd-cmd-params-patch.yaml  # --insecure flag (no TLS termination in-cluster for dev)
```

The base `kustomization.yaml` will use a remote resource reference to the upstream ArgoCD install manifest at a pinned version:

```yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: argocd
resources:
  - https://raw.githubusercontent.com/argoproj/argo-cd/v2.14.3/manifests/install.yaml
```

This keeps ArgoCD upgrades explicit (bump the tag, review the diff, apply) and avoids pulling in Helm as a dependency.

The dev overlay will patch the ArgoCD ConfigMap to register the repository:

```yaml
# argocd-cm-patch.yaml shape
apiVersion: v1
kind: ConfigMap
metadata:
  name: argocd-cm
  namespace: argocd
data:
  url: https://github.com/AaronBrownDev/direct-link
  # Kustomize build options if needed
  kustomize.buildOptions: --load-restrictor LoadRestrictionsNone
```

Repository credentials (GitHub App or deploy key) will be stored in GCP Secret Manager and synced via ESO into the `argocd` namespace, following the same pattern as `infrastructure/kubernetes/overlays/dev/eso/`.

### 2. AppProject and Application CRDs (App-of-Apps)

An "app-of-apps" root Application will manage per-environment Application resources. This keeps environment promotion explicit while avoiding manual Application creation.

**Directory structure:**

```
infrastructure/kubernetes/argocd/
  apps/
    kustomization.yaml          # includes all app-of-apps resources
    project.yaml                # AppProject: directlink
    root-app.yaml               # Application: directlink-apps (points to this directory)
    dev-app.yaml                # Application: directlink-dev
    staging-app.yaml            # Application: directlink-staging
    prod-app.yaml               # Application: directlink-prod
```

**AppProject** (`project.yaml`):

- Name: `directlink`
- Source repos: restricted to `https://github.com/AaronBrownDev/direct-link`
- Destinations: `directlink-dev` namespace on the local cluster (expand to staging/prod clusters later)
- Cluster resources: limited to namespaces, to prevent ArgoCD from managing cluster-scoped resources it should not touch
- No orphaned resource monitoring initially (keep it simple)

**Root Application** (`root-app.yaml`):

- Points to `infrastructure/kubernetes/argocd/apps/`
- Sync policy: automated with self-heal (this manages Application CRDs, not workloads)
- Project: `default` (ArgoCD's built-in project, since this app manages other apps)

**Per-environment Applications** (e.g., `dev-app.yaml`):

- Source path: `infrastructure/kubernetes/overlays/dev`
- Destination: `https://kubernetes.default.svc`, namespace `directlink-dev`
- Project: `directlink`
- Sync policy varies by environment:
  - **dev**: automated sync + self-heal + auto-prune
  - **staging**: automated sync + self-heal + auto-prune
  - **prod**: manual sync only (requires explicit approval in ArgoCD UI/CLI)
- Retry policy: 5 retries with exponential backoff (handles transient failures like image pull race conditions)
- `ServerSideApply=true` to avoid conflicts with ESO-managed secrets

### 3. CI pipeline extension: build, push, update tag

The existing `backend-ci.yml` will be extended with a new job that runs only on pushes to `main` (not on PRs). This job will:

1. Authenticate to GCP via Workload Identity Federation (see section 4)
2. Build the Docker image using `backend/Dockerfile`, passing `VERSION=$GITHUB_SHA` and `COMMIT=$GITHUB_SHA`
3. Push to Artifact Registry at `us-south1-docker.pkg.dev/directlink-dev/directlink/signaling`
4. Tag with both `$GITHUB_SHA` and `latest`
5. Use `kustomize edit set image` to update `infrastructure/kubernetes/overlays/dev/kustomization.yaml` with the new SHA tag
6. Commit and push the tag change back to `main`

**Image tag strategy**: Use the full Git SHA as the primary tag. This provides:
- Immutable, traceable tags (every deployed image maps to exactly one commit)
- Easy rollback (point the tag back to a previous SHA)
- Auditability (ArgoCD history + git log tell the complete story)

The `latest` tag is kept as a convenience for local development but is never used by ArgoCD.

**Commit-back approach**: The CI job will commit the updated `kustomization.yaml` back to `main` using a GitHub App token (not `GITHUB_TOKEN`, which cannot trigger further workflows -- this is intentional, we do NOT want the tag-update commit to re-trigger CI). The commit message will follow the conventional format: `chore(deploy): update signaling image to <short-sha>`.

To avoid infinite CI loops, the commit-back step will use `[skip ci]` in the commit message, or the workflow paths filter will naturally prevent re-triggering (since only `backend/**` paths trigger the backend CI, and the change is in `infrastructure/**`).

**Important detail**: The existing `backend-ci.yml` path filter already scopes to `backend/**` and `shared/protocol/**`. Since the kustomization change is under `infrastructure/`, it will not re-trigger the backend CI. No `[skip ci]` needed -- the path filters handle it.

### 4. Terraform: Workload Identity Federation for GitHub Actions

GitHub Actions needs to authenticate to GCP (Artifact Registry push, potentially `gcloud` commands) without long-lived service account keys. This requires a Workload Identity Federation (WIF) pool and provider.

**New Terraform module**: `infrastructure/terraform/modules/github-actions-wif/`

This module will create:

- `google_iam_workload_identity_pool`: named `github-actions`
- `google_iam_workload_identity_pool_provider`: OIDC provider for `https://token.actions.githubusercontent.com`, with attribute mappings for `google.subject` = `assertion.sub`, `attribute.repository` = `assertion.repository`
- `google_service_account`: named `github-actions-ci` with roles:
  - `roles/artifactregistry.writer` on the `directlink` Artifact Registry repository (push images)
  - No other roles initially -- ArgoCD handles deployment, CI only pushes images and commits
- `google_service_account_iam_member`: binds the WIF pool to the service account, scoped to the specific repository (`attribute.repository/AaronBrownDev/direct-link`)

The module will be wired into the dev environment at `infrastructure/terraform/environments/dev/main.tf`:

```hcl
module "github_actions_wif" {
  source = "../../modules/github-actions-wif"

  project_id  = var.project_id
  repo_owner  = "AaronBrownDev"
  repo_name   = "direct-link"
  registry_id = "directlink"
  location    = var.region
}
```

The CI workflow will use `google-github-actions/auth@v2` with the WIF provider:

```yaml
- uses: google-github-actions/auth@v2
  with:
    workload_identity_provider: projects/<project-number>/locations/global/workloadIdentityPools/github-actions/providers/github
    service_account: github-actions-ci@directlink-dev.iam.gserviceaccount.com
```

**Note**: The project number (not project ID) is required for the WIF provider path. This will be an output of the Terraform module.

### 5. ArgoCD access to Artifact Registry

ArgoCD needs to pull images from Artifact Registry to detect new image availability (if using image updater) and the cluster nodes need to pull images. Since the GKE cluster already has Workload Identity enabled and nodes have `cloud-platform` OAuth scope, GKE nodes can already pull from Artifact Registry in the same project. No additional configuration is needed for image pulls.

ArgoCD itself does not need Artifact Registry access -- it only reads Git, not container registries. The kustomization files contain the image references, and Kubernetes handles the pull.

### 6. Namespace and RBAC strategy for ArgoCD

ArgoCD will run in its own `argocd` namespace, separate from the application namespaces (`directlink-dev`, `directlink-staging`, `directlink-prod`). The `directlink` AppProject restricts what ArgoCD can deploy and where:

- Permitted source: only this repository
- Permitted destinations: only the designated application namespaces
- Denied cluster resources: everything except Namespace (ArgoCD should not manage CRDs, ClusterRoles, etc. through the application pipeline)

ESO's `ClusterSecretStore` is a cluster-scoped resource that lives outside ArgoCD's management. It was applied manually and should stay that way (or be managed by a separate infrastructure ArgoCD Application if desired later).

## Consequences

### What we gain

- **Automated delivery for dev/staging**: Push to main triggers image build, tag update, and ArgoCD sync. No manual `kubectl apply`.
- **Git as the source of truth**: Every deployed state is a commit. Rollback is `git revert` + ArgoCD sync.
- **Audit trail**: ArgoCD sync history + git log provide full traceability from commit to deployed image.
- **Environment promotion path**: staging and prod overlays can be populated incrementally. Promotion is copying/updating image tags between overlay kustomizations.
- **Drift detection**: ArgoCD continuously reconciles, catching manual `kubectl` changes.
- **No long-lived secrets in CI**: WIF eliminates service account key JSON files.

### What changes

- **ArgoCD becomes critical infrastructure**: If ArgoCD is down, deployments stop. For dev/staging this is acceptable. For prod, ArgoCD itself needs monitoring (which is outside scope of this ADR but should follow).
- **CI pipeline becomes longer**: The build-push-commit cycle adds 2-3 minutes to the pipeline on pushes to main.
- **Commit history includes automated tag updates**: These are small, predictable commits but they do add noise. The conventional commit prefix (`chore(deploy):`) makes them easy to filter.
- **Terraform state management**: The WIF module adds GCP resources that must be tracked. With the current local backend, this means the state file grows. Migrating to a remote backend (GCS) is recommended but is a separate concern.

### Risks

- **Commit-back race condition**: If two merges to main happen in quick succession, the second CI run may fail to push its tag-update commit because main has moved. Mitigation: the CI job should pull-rebase before pushing, and fail gracefully if it cannot (the next merge will pick up the correct image). Given the current team size, this is unlikely to be a practical problem.
- **ArgoCD + ESO interaction**: ArgoCD may try to "fix" secrets that ESO is managing, causing a reconciliation loop. Mitigation: use `ServerSideApply=true` and ensure ArgoCD ignores resources with `app.kubernetes.io/managed-by: external-secrets`. Add a resource exclusion in `argocd-cm` for `ExternalSecret` and `ClusterSecretStore` CRDs if ArgoCD attempts to prune them.
- **Single cluster for all environments initially**: Dev, staging, and prod namespaces on the same cluster is acceptable for now but should not be the long-term architecture. Prod should eventually be a separate cluster.
- **Spot VM preemption**: The dev cluster uses spot VMs. ArgoCD pods will restart on preemption. This is acceptable for dev but ArgoCD should be on a non-spot node pool for staging/prod.

## Implementation notes

### Phase 1: Terraform WIF (prerequisite, low risk)

**Files to create:**
- `infrastructure/terraform/modules/github-actions-wif/main.tf` -- pool, provider, service account, IAM bindings
- `infrastructure/terraform/modules/github-actions-wif/variables.tf` -- project_id, repo_owner, repo_name, registry_id, location
- `infrastructure/terraform/modules/github-actions-wif/outputs.tf` -- workload_identity_provider (full path), service_account_email

**Files to modify:**
- `infrastructure/terraform/environments/dev/main.tf` -- add `module "github_actions_wif"` block

**Apply**: `terraform plan` and `terraform apply` from `infrastructure/terraform/environments/dev/`.

**Verification**: The WIF provider and SA should appear in the GCP console under IAM > Workload Identity Federation.

### Phase 2: CI pipeline extension (depends on Phase 1)

**Files to modify:**
- `.github/workflows/backend-ci.yml` -- add a `deploy` job gated on `github.event_name == 'push'` and `github.ref == 'refs/heads/main'`, dependent on `build-and-test` and `lint` succeeding.

**New job structure (conceptual, not implementation):**

The `deploy` job should:
1. Check out the repo
2. Authenticate to GCP using the WIF provider output from Phase 1
3. Configure Docker for Artifact Registry (`gcloud auth configure-docker us-south1-docker.pkg.dev`)
4. Build the image with `docker build -f backend/Dockerfile --build-arg VERSION=$GITHUB_SHA --build-arg COMMIT=$GITHUB_SHA -t us-south1-docker.pkg.dev/directlink-dev/directlink/signaling:$GITHUB_SHA -t us-south1-docker.pkg.dev/directlink-dev/directlink/signaling:latest .`
5. Push both tags
6. Install kustomize, run `kustomize edit set image directlink/signaling=us-south1-docker.pkg.dev/directlink-dev/directlink/signaling:$GITHUB_SHA` in `infrastructure/kubernetes/overlays/dev/`
7. Commit and push the kustomization change with a GitHub App token or PAT

**GitHub Actions permissions:**

```yaml
permissions:
  contents: write          # commit-back
  id-token: write          # WIF OIDC token
```

**Verification**: Merge a change to `backend/`, confirm the image appears in Artifact Registry, and the kustomization.yaml is updated with the new SHA tag.

### Phase 3: ArgoCD installation (independent of Phase 2, but deploy after)

**Files to create:**
- `infrastructure/kubernetes/argocd/base/kustomization.yaml` -- upstream install reference at pinned version
- `infrastructure/kubernetes/argocd/overlays/dev/kustomization.yaml` -- namespace, patches
- `infrastructure/kubernetes/argocd/overlays/dev/argocd-cm-patch.yaml` -- repo config, kustomize build options
- `infrastructure/kubernetes/argocd/overlays/dev/argocd-cmd-params-patch.yaml` -- `--insecure` for dev

**Bootstrap**: ArgoCD must be bootstrapped manually the first time (it cannot deploy itself from nothing). Run `kubectl apply -k infrastructure/kubernetes/argocd/overlays/dev/` from a workstation or CI with cluster access. After this, ArgoCD manages itself via the app-of-apps pattern.

**Initial admin access**: ArgoCD generates a random admin password stored in `argocd-initial-admin-secret`. Retrieve with `kubectl -n argocd get secret argocd-initial-admin-secret -o jsonpath='{.data.password}' | base64 -d`. Change it immediately or disable admin login in favor of SSO (out of scope for this ADR).

**Port-forward for dev access**: `kubectl port-forward svc/argocd-server -n argocd 8080:443`. No Ingress needed for dev.

### Phase 4: Application CRDs and App-of-Apps (depends on Phase 3)

**Files to create:**
- `infrastructure/kubernetes/argocd/apps/kustomization.yaml`
- `infrastructure/kubernetes/argocd/apps/project.yaml` -- AppProject `directlink`
- `infrastructure/kubernetes/argocd/apps/root-app.yaml` -- self-referencing root app
- `infrastructure/kubernetes/argocd/apps/dev-app.yaml` -- points to `infrastructure/kubernetes/overlays/dev`

**Staging and prod apps**: Create `staging-app.yaml` and `prod-app.yaml` stubs that point to the (currently empty) overlay directories. ArgoCD will show them as "OutOfSync" with nothing to sync until overlays are populated. Alternatively, defer creating these until the overlays have content.

**Verification**: After applying, check ArgoCD UI (port-forwarded) -- the root app should show healthy, and the dev app should sync the directlink-dev namespace.

### Phase 5: ESO integration for ArgoCD repo credentials

**Files to create:**
- `infrastructure/kubernetes/argocd/overlays/dev/eso/external-secret.yaml` -- syncs GitHub App private key or deploy key from GCP Secret Manager into ArgoCD's `repo-creds` secret format

**Prerequisite**: Store the GitHub credential (App private key or deploy key) in GCP Secret Manager. The credential must have read access to the repository.

ArgoCD expects repository credentials in a specific Secret format with the label `argocd.argoproj.io/secret-type: repository`. The ExternalSecret should target this format.

If the repository is public, this phase can be skipped entirely. ArgoCD can read public repos without credentials.

## Open questions

1. **Is the repository public or private?** If public, Phase 5 (repo credentials) is unnecessary. If private, a GitHub App or deploy key must be provisioned and stored in Secret Manager.

2. **GitHub App vs. PAT for commit-back?** A GitHub App installation token is preferred (scoped permissions, no personal account dependency, auto-rotating). But it requires creating a GitHub App. A fine-grained PAT scoped to this repo is simpler for a small team. Decision needed before Phase 2.

3. **ArgoCD version**: This document references v2.14.3 as an example. The implementer should pin to the latest stable release at implementation time and record it.

4. **Artifact Registry repository**: The dev overlay references `us-south1-docker.pkg.dev/directlink-dev/directlink/signaling`. Is the `directlink` Artifact Registry repository already created? If not, it should be added to Terraform (a `google_artifact_registry_repository` resource in the dev environment or a shared module).

5. **Remote Terraform backend**: The current local backend means Terraform state is on one person's machine. This is fragile. Migrating to a GCS backend is strongly recommended before adding more infrastructure (WIF resources) but is a separate decision.

6. **Notifications**: ArgoCD can send sync status to Slack, GitHub commit statuses, etc. via argocd-notifications. Worth setting up but should be a follow-on, not a blocker.

7. **Resource limits for ArgoCD**: On a single-node spot cluster with `e2-standard-2` (2 vCPU, 8GB RAM), ArgoCD's resource footprint matters. The default ArgoCD install requests are modest (~250m CPU, ~256Mi per component), but with 5 components (server, repo-server, application-controller, redis, dex) the total adds up. Monitor and tune after deployment. Consider whether the dev cluster node size needs to increase.
