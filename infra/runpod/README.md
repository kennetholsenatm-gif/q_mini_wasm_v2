# Runpod + OpenTofu (on-demand GPU)

**Quick checklist:** [docs/RUNPOD_QUICKSTART.md](../../docs/RUNPOD_QUICKSTART.md) — helper scripts: `scripts/runpod_prereqs.sh`, `scripts/runpod_bootstrap.sh`, `scripts/runpod_destroy.sh`.

Use **OpenTofu** (or Terraform) with the official **[Runpod provider](https://registry.terraform.io/providers/decentralized-infrastructure/runpod/latest)** so you can:

- **`tofu apply`** — create the pod (start paying)
- **`tofu destroy`** — delete the pod (stop paying)

This repo standardizes on **`RUNPOD_TOKEN`** in `.env`. The provider expects **`RUNPOD_API_KEY`** — use the wrapper scripts below or `export RUNPOD_API_KEY="$RUNPOD_TOKEN"` before running `tofu`.

## Prerequisites

- [OpenTofu](https://opentofu.org/docs/intro/install/) or Terraform ≥ 1.0
- Runpod API key in [Runpod console → Settings](https://www.runpod.io/console/user/settings)

## Cloud GPU (CUDA) on the pod

Default **`env`** sets `ACCELERATOR=cuda` and `NVIDIA_VISIBLE_DEVICES=all` so training can use the GPU after you SSH in and install the package. See **[CLOUD_ACCELERATOR.md](CLOUD_ACCELERATOR.md)** and **`scripts/runpod_sync_and_train.example.sh`** for rsync + remote `python -m qminiwasm.engine` flow.

## One-time setup

```bash
cd infra/runpod
cp terraform.tfvars.example terraform.tfvars   # optional: tune GPU / image / region
./tofu.sh init
```

Edit **`terraform.tfvars`** (gitignored) for your GPU template, image, ports, etc. **`gpu_type_ids`** values are RunPod **GPU type ids** (often snake_case strings from the API/console, e.g. `actual_coffee_meadowlark`), not only display labels — adjust with `data_center_ids` and `image_name` to match what Runpod offers in your account.

## Day-to-day (pay only when up)

**Start pod (incur cost):**

```bash
cd infra/runpod
# Load RUNPOD_TOKEN from repo .env (PowerShell: see below)
./tofu.sh apply
```

**Stop pod (stop billing):**

```bash
cd infra/runpod
./tofu.sh destroy
```

## Token: `RUNPOD_TOKEN` → provider

The provider reads **`RUNPOD_API_KEY`**. From bash (WSL / Linux / Git Bash), after `cd` to repo root:

```bash
set -a && source .env && set +a   # only if .env has simple KEY=value lines
export RUNPOD_API_KEY="${RUNPOD_TOKEN}"
cd infra/runpod && tofu apply
```

Or use **`./tofu.sh`** from `infra/runpod`: it sets `RUNPOD_API_KEY` from `RUNPOD_TOKEN` if set.

**PowerShell** (repo root):

```powershell
Get-Content .env | ForEach-Object { if ($_ -match '^([^#=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1].Trim(), $matches[2].Trim(), 'Process') } }
$env:RUNPOD_API_KEY = $env:RUNPOD_TOKEN
cd infra\runpod; tofu apply
```

## State files

State is **local** by default (`terraform.tfstate` in this directory). It is **gitignored**. For a team, use a remote backend (S3, etc.) — add a `backend` block in `versions.tf`.

## Files

| File | Purpose |
|------|---------|
| `versions.tf` | Provider pin |
| `variables.tf` | Inputs |
| `main.tf` | `runpod_pod` resource |
| `outputs.tf` | Pod id, IP, cost hints |
| `terraform.tfvars.example` | Copy to `terraform.tfvars` |
| `tofu.sh` | Maps `RUNPOD_TOKEN` → `RUNPOD_API_KEY`, runs `tofu` |

## References

- Provider docs: [runpod_pod](https://registry.terraform.io/providers/decentralized-infrastructure/runpod/latest/docs/resources/pod)
- Upstream examples: [terraform-provider-runpod/examples](https://github.com/decentralized-infrastructure/terraform-provider-runpod/tree/main/examples)
