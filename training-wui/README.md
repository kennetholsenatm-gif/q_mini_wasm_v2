# Training WUI (Go)

Small web UI to pick a `configs/training/*.toml` file and run `python -m engine --config …` from the **repository root** (so `engine` and `qminiwasm` resolve and `.env` is found by the Python loader).

## Requirements

- [Go](https://go.dev/dl/) 1.22+
- Python env with the package installed (`pip install -e ".[training]"`) and `python` on `PATH`

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

### Flags / environment

| Flag | Env | Default | Meaning |
|------|-----|---------|---------|
| `-addr` | `TRAINING_WUI_ADDR` | `:8765` | Listen address |
| `-root` | `TRAINING_WUI_ROOT` | `.` | **Absolute path to repo root** (parent of `configs/` and `engine/`) |
| `-python` | `TRAINING_WUI_PYTHON` | `python` | Python executable |

Only one training process at a time is allowed (start another after the current run finishes or after **Stop**).

### RunPod (OpenTofu)

The dashboard can target **RunPod** so OpenTofu runs **`apply`** before `python -m engine` and **`destroy`** when the job exits or you **Stop** (optional checkbox: skip destroy if you want to keep the pod).

- Set **`RUNPOD_TOKEN`** (or `RUNPOD_API_KEY`) in the repo **`.env`**; the WUI loads it on startup (`loadDotenvFromRepo`).
- Stack lives in **`infra/runpod`** (see that folder’s README). Install **OpenTofu** (`tofu`) or Terraform on `PATH`.
- **Training still runs on the same host as the WUI** (subprocess). The RunPod pod is provisioned for GPU / remote work; use **`public_ip`** from status outputs and SSH into the pod if you want training on the GPU there.

**Cloud GPU (CUDA):** Pods default to **`ACCELERATOR=cuda`** in container env (`infra/runpod/variables.tf`). See **`infra/runpod/CLOUD_ACCELERATOR.md`** and **`scripts/runpod_sync_and_train.example.sh`** for rsync + SSH + `python -m engine` on the pod.

**Infrastructure card** (top of the UI): status badges (token / `infra/runpod` / OpenTofu), tofu outputs (`public_ip`, pod id, cost), **Copy pod IP** / **Copy remote commands**, and a **Remote GPU** foldout with rsync/SSH snippets. **Log** is at the **bottom**, full page width.

`GET /api/runpod/status` — token, binary, `tofu output` (when state exists). Start APIs accept `run_target`: `"local"` | `"runpod"` and `runpod_destroy_on_exit` (default true for RunPod).

**Preflight FAQ**

- **CPU vs Build wizard accelerator**: Preflight loads the TOML from the **left config dropdown** (e.g. `cascade_mopd.toml` has `accelerator = "cpu"`). The **Build + Run** form’s accelerator is sent as `?accelerator=` so device resolution matches what you intend to train with, without editing that file.
- **IBM “pending jobs” = 0**: That field is the **backend queue depth** (jobs waiting on that IBM device). Zero does not mean “no quantum access”; it usually means nothing is queued right now. Training may still use a local/simulator path until it submits hardware jobs.
- **HF “dataset config” (none)**: Optional subset name for multi-config datasets. Empty means the **default** config on Hugging Face; Model Facts shows `(none)` when omitted.

## Incus container (in this repo)

On the machine where `incus` runs: **`training-wui/incus/setup-instance.sh`** and how to start the WUI — see [`incus/README.md`](incus/README.md). Repo root in the guest is **`/opt/qmw`** (`-root` for the WUI).

## Security note

This tool is for **local development**. It does not authenticate clients and can start arbitrary-length training jobs. Do not expose `-addr` on a public network without a reverse proxy and auth.
