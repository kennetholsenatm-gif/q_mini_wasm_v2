# OpenTofu (IaC)

This directory contains OpenTofu (Terraform-compatible) configurations for infrastructure and Kubernetes.

## Layout

| Path | Purpose |
|------|---------|
| **environments/local/** | Kind cluster + NGINX Ingress + Teleport (and optional WUI). See [environments/local/README.md](environments/local/README.md) for dependencies and commands. |
| **environments/prod/** | Stub for production (EKS/AKS/GKE); reuses same addons/workloads as local. |
| **modules/cluster/kind/** | Kind config (ports 80/443, Pod Security) and Makefile/script to create the local cluster. |
| **modules/addons/** | NGINX Ingress Controller (Helm). |
| **modules/workloads/** | Teleport and optional qminiwasm-wui (Helm). |
| **kubernetes/** | Deploy Teleport to an **existing** cluster (flat config; used by CI or when cluster is created elsewhere). |
| **keycloak-provisioning/** | Keycloak realm, groups, and users via mrparkers/keycloak provider. |
| **kyverno/**, **falco/** | Policy and security (if present). |

## Quick start (local dev)

1. **Dependencies:** Docker, OpenTofu, Kind, kubectl.
2. **Create cluster:** `make -C modules/cluster/kind kind-create` (from repo root); then `export KUBECONFIG=.../environments/local/kubeconfig`.
3. **Deploy:** `cd environments/local && tofu init && tofu apply` (after copying `terraform.tfvars.example` to `terraform.tfvars` and setting `teleport_values_file`).

Full steps: [environments/local/README.md](environments/local/README.md).
