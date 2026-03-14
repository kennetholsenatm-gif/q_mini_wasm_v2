# Production environment (stub)

This directory is a placeholder for production Kubernetes deployments (e.g. AWS EKS, Azure AKS, GCP GKE).

## Intended use

- **Cluster provisioning:** Use a cloud-specific cluster module (e.g. `modules/cluster/eks`, `modules/cluster/aks`, or `modules/cluster/gke`) to create the cluster and obtain kubeconfig or OIDC configuration.
- **Application deployment:** Reuse the same **addons** and **workloads** modules as in [../local/](../local/):
  - `module "addons"` — NGINX Ingress (or cloud LB ingress) with `host_port_enabled = false`
  - `module "workloads"` — Teleport and optional qminiwasm-wui with production values and secrets from Vault or env

## Steps to add a production environment

1. Create the cluster module (e.g. under `modules/cluster/eks`) and wire its outputs (kubeconfig path or in-cluster auth) to the Kubernetes and Helm providers.
2. Copy or adapt `environments/local/main.tf`, `variables.tf`, and `outputs.tf` into this directory.
3. Point `teleport_values_file` and workload config at production values; use a remote backend and variables (e.g. from Vault) for secrets.
4. Run `tofu init`, `tofu plan`, and `tofu apply` with the appropriate backend and var files.

No application deployment logic needs to change; only the cluster source and provider configuration differ from local.
