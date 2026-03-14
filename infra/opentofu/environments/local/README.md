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

## Commands (script-based cluster)

### 1. Create the Kind cluster

From the **repository root**:

```bash
make -C infra/opentofu/modules/cluster/kind kind-create
```

Or from this directory:

```bash
../../modules/cluster/kind/create-cluster.sh
```

This creates a cluster named `qminiwasm-local` with host ports 80 and 443 exposed and writes kubeconfig to `infra/opentofu/environments/local/kubeconfig`.

Set kubeconfig for the next step:

```bash
export KUBECONFIG="$(pwd)/infra/opentofu/environments/local/kubeconfig"
```

(Or `cd infra/opentofu/environments/local` and use `kube_config_path = "./kubeconfig"` in tfvars.)

### 2. Apply OpenTofu (addons + workloads)

```bash
cd infra/opentofu/environments/local
cp terraform.tfvars.example terraform.tfvars
# Edit terraform.tfvars: set teleport_values_file path and optional enable_wui
tofu init
tofu plan
tofu apply
```

This installs NGINX Ingress (host 80/443) and Teleport (and optionally qminiwasm-wui).

### 3. Verify

```bash
kubectl get pods -A
```

- Ingress: **http://localhost** (and **https://localhost** if TLS is configured).
- Teleport: configure Ingress or port-forward to the Teleport proxy service; use the URL from your Teleport values (e.g. `teleport.example.com` with a local hosts entry or ingress host).

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

- Delete the Kind cluster: `make -C infra/opentofu/modules/cluster/kind kind-delete`
- OpenTofu destroy (optional): from `environments/local`, run `tofu destroy` to remove Helm releases; the cluster remains until you run `kind delete cluster`.
