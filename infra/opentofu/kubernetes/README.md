# Kubernetes (Teleport) OpenTofu module

Deploys Teleport to an existing Kubernetes cluster via the official Helm chart.

## Prerequisites

- OpenTofu 1.0+
- Kubernetes cluster and kubeconfig (e.g. `KUBECONFIG` env or `kube_config_path` in tfvars)
- Helm and Kubernetes providers

## Usage

From repository root:

```bash
cd infra/opentofu/kubernetes
tofu init
tofu plan -var-file=../../desired/kubernetes-teleport.tfvars.json
tofu apply -var-file=../../desired/kubernetes-teleport.tfvars.json -auto-approve
```

CI (`.github/workflows/opentofu-infra.yml`) runs plan on push/PR and apply on main when `infra/opentofu/desired/*.tfvars.json` changes. Ensure `KUBECONFIG` or cluster access is configured in the CI environment.

## Variables

- `teleport_values_file`: Path to Helm values (e.g. `../../teleport/teleport-values.yaml`). No secrets in the values file; use K8s secrets for OIDC and audit sink.
- `teleport_chart_version`: Pin to the Teleport version you need (e.g. `16.1.0`).

Do not commit secrets in tfvars or values; use environment variables or a secret store (e.g. Vault) for OIDC client secret and audit endpoints.
