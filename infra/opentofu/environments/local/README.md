# Local Kubernetes development environment (Kind + OpenTofu)

Reproducible local cluster using **Kind** (Kubernetes IN Docker), with **OpenTofu** deploying NGINX Ingress and workloads (Teleport, optional WUI). The same addon and workload logic can be reused for production (EKS/AKS/GKE) by swapping the cluster source; see [../prod/README.md](../prod/README.md).

## Dependencies

Install before running:

| Tool | Purpose |
|------|---------|
| **Docker** | Required for Kind (containers run inside Docker). |
| **OpenTofu** 1.x | IaC for addons and workloads. [Install OpenTofu](https://opentofu.org/docs/intro/install/). |
| **Kind** | Creates the local Kubernetes cluster. `go install sigs.k8s.io/kind@latest` or use your package manager. |
| **kubectl** | Optional but recommended to verify pods and access the cluster. |

## Commands

### Easiest: one command from repo root

```bash
make -C infra/opentofu local-up
```

This checks dependencies (Docker, Kind, OpenTofu), creates the Kind cluster if it does not exist, and runs `tofu init` and `tofu apply` using `terraform.tfvars.example` by default (no copy required). To tear down: `make -C infra/opentofu local-down`.

### Manual steps (create cluster, then apply)

**1. Create the Kind cluster** (from repository root):

```bash
make -C infra/opentofu/modules/cluster/kind kind-create
```

Or from this directory: `../../modules/cluster/kind/create-cluster.sh`. This writes kubeconfig to `infra/opentofu/environments/local/kubeconfig`.

**2. Apply OpenTofu** (from repo root or from this directory):

```bash
export KUBECONFIG="$(pwd)/infra/opentofu/environments/local/kubeconfig"   # from repo root
cd infra/opentofu/environments/local
# Optional: cp terraform.tfvars.example terraform.tfvars and edit (e.g. teleport_values_file, enable_wui)
tofu init
tofu plan
tofu apply
```

If you do not create `terraform.tfvars`, use the example as the var file: `tofu apply -var-file=terraform.tfvars.example`.

### Verify

1. Export kubeconfig (from repo root):

   ```bash
   export KUBECONFIG="$(pwd)/infra/opentofu/environments/local/kubeconfig"
   ```

2. Check all pods and Teleport:

   ```bash
   kubectl get pods -A
   kubectl get pods -n teleport
   ```

3. **If Teleport auth is not Running:** See [infra/teleport/README.md](../../teleport/README.md): delete the auth PVC, restart the auth pod, and confirm the release uses `teleport-values-local.yaml` and chart 16.1.0.

- Ingress: **http://localhost** (and **https://localhost** if TLS is configured).
- **Teleport (local):** The example tfvars set `teleport_wait = false` and `teleport_wait_for_jobs = false` so `local-up` completes without waiting on Teleport. If auth is CrashLooping, follow the troubleshooting in [infra/teleport/README.md](../../teleport/README.md).

### Local Kind: access via Teleport

For Zero Trust parity you can access the cluster through Teleport instead of direct kubeconfig:

1. Port-forward the Teleport proxy (from a terminal with `KUBECONFIG` set):

   ```bash
   kubectl port-forward -n teleport svc/teleport 443:443
   ```

2. In another terminal, log in with a local user (create one first via `tctl users add` from inside the cluster, or use the auth pod):

   ```bash
   tsh login --proxy=localhost:443 --user=<local-user>
   ```

3. Attach to the Kubernetes cluster (name must match `kubeClusterName` in [teleport-values-local.yaml](../../teleport/teleport-values-local.yaml), e.g. `qminiwasm-local`):

   ```bash
   tsh kube login qminiwasm-local
   ```

   Then use `kubectl` as usual; access is gated by Teleport. For local dev, many users use direct `KUBECONFIG`; Teleport is optional.

## Optional: WUI

To deploy the qminiwasm-wui chart, set in `terraform.tfvars`:

```hcl
enable_wui   = true
wui_chart_path = "../../../../charts/qminiwasm-wui"   # from repo root when running in environments/local
```

Then `tofu apply`. The chart uses non-root security context and the nginx IngressClass.

## Production: swapping the cluster

The **addons** and **workloads** modules are cluster-agnostic. For production:

1. Use a cloud cluster module (e.g. `modules/cluster/eks`) to create the cluster and obtain kubeconfig or in-cluster auth.
2. In `environments/prod`, use the same `module "addons"` and `module "workloads"` with `host_port_enabled = false` for ingress and production values/secrets (e.g. from Vault).

See [../prod/README.md](../prod/README.md).

## Cleanup

- **One command:** `make -C infra/opentofu local-down` (runs `tofu destroy` then deletes the Kind cluster).
- **Manual:** from `environments/local`, run `tofu destroy` to remove Helm releases; then `make -C infra/opentofu/modules/cluster/kind kind-delete` to remove the cluster.
