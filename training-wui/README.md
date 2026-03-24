# Training WUI (Go)

Small web UI to pick a `configs/training/*.toml` file and run `python -m engine --config …` from the **repository root** (so `engine` and `qminiwasm` resolve and `.env` is found by the Python loader).

## Requirements

- [Go](https://go.dev/dl/) 1.22+
- Python env with the package installed (`pip install -e ".[training]"`) and `python` on `PATH`
- **RunPod (optional):** [OpenTofu](https://opentofu.org/docs/intro/install/) **`tofu`** or HashiCorp **Terraform** on `PATH` (the server shells out to `tofu` / `terraform` under `infra/runpod`). Incus/host automation now lives in `C:\GiTeaRepos\System_admin\runbooks\qminiwasm\incus`.

## Run

From this directory:

```bash
go run . -root .. -addr :8765
```

Or build:

```bash
go build -o training-wui .
./training-wui -root ..
```

Open [http://127.0.0.1:8765](http://127.0.0.1:8765).

### Flags

| Flag | Default | Meaning |
|------|---------|---------|
| `-addr` | `:8765` | Listen address (`host:port`) |
| `-root` | `.` | **Repo root** (directory that contains `configs/` and `engine/`) |
| `-python` | `python` | Python executable name or path on `PATH` |

### Tabs (workflow)

- **Launchpad** — Start from an existing TOML, **Build + Run**, preflight, runs list, log, and a shortcut to **Configuration**.
- **Configuration** — **Schema-driven custom run**: all guided fields from `/api/schema` (hardware, training, data, Hugging Face revision, WASM limits, checkpoints, …). Writes `configs/training/<name>.toml` and starts training — no CLI or hand-edited files for normal use.
- **Dataset Builder** — Curated + **custom Hub dataset ids** for a multi-dataset mix. Selections are merged into the custom-run payload as `data.path = qminiwasm/hf-multi` and `[huggingface].extra_specs`. **Apply mix** syncs Launchpad and the Configuration form; you can also select datasets and start directly from Configuration (live mix is picked up).
- **Mission Control**, **Node Health**, **Quantum Topology**, **Artifact Registry** — unchanged.

Only one training process at a time is allowed (start another after the current run finishes or after **Stop**).

### RunPod (OpenTofu)

The dashboard can target **RunPod** so OpenTofu runs **`apply`** before `python -m engine` and **`destroy`** when the job exits or you **Stop** (optional checkbox: skip destroy if you want to keep the pod).

- Set **`RUNPOD_TOKEN`** (or `RUNPOD_API_KEY`) in the repo **`.env`**; the WUI loads it on startup (`loadDotenvFromRepo`).
- Stack lives in **`infra/runpod`** (see that folder’s README). **`tofu` or `terraform` must be on `PATH`** (OpenTofu releases: `tofu`; install script now in **`C:\GiTeaRepos\System_admin\runbooks\qminiwasm\incus\install-opentofu.sh`** for Linux/Incus).
- **Training still runs on the same host as the WUI** (subprocess). The RunPod pod is provisioned for GPU / remote work; use **`public_ip`** from status outputs and SSH into the pod if you want training on the GPU there.

**Cloud GPU (CUDA):** Pods default to **`ACCELERATOR=cuda`** in container env (`infra/runpod/variables.tf`). See **`infra/runpod/CLOUD_ACCELERATOR.md`** and **`scripts/runpod_sync_and_train.example.sh`** for rsync + SSH + `python -m engine` on the pod.

**Infrastructure tab:** edit **`infra/runpod/terraform.tfvars`** in the browser (Save / Reload / Insert example from `terraform.tfvars.example`), then **Status** (token / `infra/runpod` / `tofu`, outputs, copy buttons, **Remote GPU** snippets) and **OpenTofu CLI** (`tofu init` / `plan` / `apply` / `destroy` — use **plan** to debug apply exit 1). **Training** tab stays focused on configs and runs; **log** stays at the bottom on both tabs.

**Floating panels (optional):** On **Launchpad** (Training) and **Infrastructure**, enable **Floating panels** to drag sections by their **title** and resize from the **corner grip**. On **Launchpad** this includes the **Log** panel (live output). Layout is stored in **`localStorage`**. The log stays on **Launchpad**; open **Configuration** for the guided form without floating layout there.

### Agent / inference outputs (Build + Run)

Each **Build + Run** writes training checkpoints under **`artifacts/models/<model-slug>/`** (repo root, gitignored):

| File | Role |
|------|------|
| `final.pt` | End-of-run weights (`CHECKPOINT_SAVE_PATH`) |
| `best.pt` | Best training MSE so far (`CHECKPOINT_BEST_PATH`) — **default for serving** |
| `latest.pt` | Last epoch (`CHECKPOINT_LATEST_PATH`) — used for **resume** |
| `serve.toml` | Minimal `[serve]` table; load with **`QMINIWASM_SERVE_CONFIG=artifacts/models/<slug>/serve.toml`** |
| `agent_bundle.json` | Machine-readable paths + **`uvicorn engine.serve:app`** hint + HTTP API summary |

After training, point tools or agents at **`agent_bundle.json`** or set **`QMINIWASM_CHECKPOINT=artifacts/models/<slug>/best.pt`** and run **`uvicorn engine.serve:app`** (see repo **`engine/serve.py`**, **`pip install -e ".[serve]"`**). Inference is **`POST /infer`** with **`hidden_states`** (batch of 4096-float vectors); see the bundle JSON for the exact contract.

`GET /api/runpod/status` — token, binary, `tofu output` (when state exists). `GET` / `PUT /api/runpod/tfvars` — read or write **`terraform.tfvars`** only (`PUT` body `{ "content": "…" }`; `GET ?source=example` returns the example file). `POST /api/runpod/tofu` — JSON `{ "action": "init"|"plan"|"apply"|"destroy", "var_file": "terraform.tfvars" }` (`var_file` optional). Start APIs accept `run_target`: `"local"` | `"runpod"`, `runpod_destroy_on_exit`, and optional **`runpod_var_file`** (basename under `infra/runpod`; UI defaults to `terraform.tfvars`).

**Preflight FAQ**

- **CPU vs Build wizard accelerator**: Preflight loads the TOML from the **left config dropdown** (e.g. `cascade_mopd.toml` has `accelerator = "cpu"`). The **Build + Run** form’s accelerator is sent as `?accelerator=` so device resolution matches what you intend to train with, without editing that file.
- **IBM “pending jobs” = 0**: That field is the **backend queue depth** (jobs waiting on that IBM device). Zero does not mean “no quantum access”; it usually means nothing is queued right now. Training may still use a local/simulator path until it submits hardware jobs.
- **HF “dataset config” (none)**: Optional subset name for multi-config datasets. Empty means the **default** config on Hugging Face; Model Facts shows `(none)` when omitted.

## Incus container (ops repo)

On the machine where `incus` runs (e.g. WSL):

```bash
cd /mnt/c/GiTeaRepos/System_admin/runbooks/qminiwasm/incus
chmod +x run-setup.sh setup-instance.sh install-opentofu.sh install-systemd-wui.sh
./run-setup.sh
```

Or **`./setup-instance.sh /absolute/path/to/qminiwasm-core`**. Full details, autostart, and troubleshooting are in the `System_admin` runbook README. In the guest, repo root remains **`/opt/qmw`** (WUI `-root`).

## Security note

This tool is for **local development**. It does not authenticate clients and can start arbitrary-length training jobs. Do not expose `-addr` on a public network without a reverse proxy and auth.
