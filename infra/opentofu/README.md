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

**One command** (from repo root):

```bash
make -C infra/opentofu local-up
```

This checks Docker/Kind/OpenTofu, creates the Kind cluster if needed, and applies OpenTofu (NGINX Ingress + Teleport). No need to copy `terraform.tfvars`—defaults use `terraform.tfvars.example`. To tear down: `make -C infra/opentofu local-down`.

**Dependencies:** Docker, OpenTofu, Kind (kubectl optional). Full steps and overrides: [environments/local/README.md](environments/local/README.md).
