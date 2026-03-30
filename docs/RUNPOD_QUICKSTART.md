# RunPod quickstart (repo-aligned)

End-to-end checklist for GPU pods via `infra/runpod` and optional remote training. For architecture details see [infra/runpod/README.md](../infra/runpod/README.md), [CLOUD_ACCELERATOR.md](../infra/runpod/CLOUD_ACCELERATOR.md), and [docs/planning/RUNPOD_WUI.md](planning/RUNPOD_WUI.md).

## 0. Automated checks (optional)

From the **repository root** (bash / WSL / Git Bash):

```bash
./scripts/runpod_prereqs.sh    # verifies token, OpenTofu/Terraform, terraform.tfvars
./scripts/runpod_bootstrap.sh  # creates tfvars from example if missing, init, plan
```

To **create the pod** after a clean plan:

```bash
APPLY=1 ./scripts/runpod_bootstrap.sh
# or: cd infra/runpod && ./tofu.sh apply
```

To **tear down** (stop billing):

```bash
./scripts/runpod_destroy.sh
```

## 1. RunPod account and API key

1. Log in at [runpod.io](https://www.runpod.io/) and add billing if required.
2. Create an API key: **Console → User settings → API keys**.
3. Copy [`.env.example`](../.env.example) to `.env` (gitignored) and set **`RUNPOD_TOKEN=...`** (or `RUNPOD_API_KEY`).

If you use the **training WUI**, it loads repo `.env` at startup — **restart the WUI** after changing `.env` so `/api/runpod/*` sees the token.

## 2. OpenTofu or Terraform

Install **`tofu`** or **`terraform`** and ensure it is on `PATH`:

- OpenTofu: [Install](https://opentofu.org/docs/intro/install/)
- Terraform: [Install](https://developer.hashicorp.com/terraform/install)

**Important:** Run **`tofu init`** / **`tofu plan`** only inside **`infra/runpod`**, not the repo root — the root has no `*.tf` files, so OpenTofu will report an “empty directory”.

**PowerShell** (from repo root, with `RUNPOD_TOKEN` already set):

```powershell
cd infra\runpod
if (-not $env:RUNPOD_API_KEY -and $env:RUNPOD_TOKEN) { $env:RUNPOD_API_KEY = $env:RUNPOD_TOKEN }
tofu init
tofu plan
```

The WUI resolves `tofu` first, then `terraform` ([`training-wui/runpod.go`](../training-wui/runpod.go)).

## 3. `terraform.tfvars`

`*.tfvars` is gitignored. Create or tune **`infra/runpod/terraform.tfvars`**:

```bash
cp infra/runpod/terraform.tfvars.example infra/runpod/terraform.tfvars
```

Edit **`gpu_type_ids`**, **`data_center_ids`**, and optionally **`image_name`** to match offerings in your RunPod account. GPU entries are often **internal type ids** (e.g. `actual_coffee_meadowlark`) from the deploy UI/API, not only human-readable names. Repo default in `variables.tf` prefers `actual_coffee_meadowlark` first, then common RTX names. If `plan` / `apply` fails, adjust until the plan succeeds.

`scripts/runpod_bootstrap.sh` copies the example file automatically if `terraform.tfvars` is missing.

## 4. Init and apply

```bash
cd infra/runpod
./tofu.sh init
./tofu.sh plan
./tofu.sh apply   # starts billing
```

Or use the WUI **Infrastructure** tab: OpenTofu actions (`init` / `plan` / `apply` / `destroy`).

## 5. Training: full WUI flow (recommended)

1. **Infra & RunPod** tab: save **`terraform.tfvars`**, run **tofu init** / **plan** / **apply** (or let **Launch training** run **apply** for you).
2. Ensure the WUI machine has **`ssh`**, **`tar`**, and **`scp`** on **`PATH`** (Windows: optional OpenSSH Client). Adjust **`configs/wui.toml`** **`[wui.runpod]`** (`ssh_user`, `remote_dir`, `ssh_key_path`) if defaults (`root`, `/workspace/qminiwasm-core`, empty key path) are wrong.
3. **Training** tab → step 2: **Execution target** = **RunPod**. Leave **Train on RunPod GPU** checked (default). **Launch training** runs **apply** (unless **Skip OpenTofu apply**), waits for **`public_ip`**, **syncs the repo** over SSH, then runs the **native** training path on the pod: start **`qminiwasm_training_engine_server`** (C++ LibTorch gRPC) and **`go run ./cmd/qmw-grpc-train`** from **`training-wui/`** (see [`training-wui/runpod_remote.go`](../training-wui/runpod_remote.go)). Logs stream in **Mission Control** like a local run. The pod must have a **built** training server binary, **Go** on `PATH`, and **LibTorch** libraries available (same requirements as local native training).

Uncheck **Train on RunPod GPU** only if you want training to run **on the WUI host** over **gRPC** to a local C++ engine while still provisioning a pod (unusual).

**Manual CLI alternative:** [CLOUD_ACCELERATOR.md](../infra/runpod/CLOUD_ACCELERATOR.md) and **`scripts/runpod_sync_and_train.example.sh`** — same idea without the WUI automation.

**Alternative:** run the WUI **inside** the pod if you want zero SSH from your laptop (then use **local** target on the pod).

**RunPod Serverless** (queue / worker jobs, no OpenTofu in that path): see **[RUNPOD_SERVERLESS.md](RUNPOD_SERVERLESS.md)**.

## 6. Secrets on the pod

Copy or recreate **`.env`** on the pod (or export vars) for `HUGGING_FACE_HUB_TOKEN` / `HF_TOKEN`, IBM Quantum tokens, etc., so the synced tree matches what the native engine and data loaders expect. Training on the pod uses **native gRPC** (`qminiwasm_training_engine_server` + `qmw-grpc-train`); see [docs/TRAINING_DATA.md](TRAINING_DATA.md) for TOML and data-source reference.

## 7. Destroy when done

```bash
./scripts/runpod_destroy.sh
# or: cd infra/runpod && ./tofu.sh destroy
```

Or the WUI **destroy** action. Uncheck **destroy on exit** in the UI if you want the pod to survive between training runs.
