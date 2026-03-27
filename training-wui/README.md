# Training WUI (Go)

Small web UI to pick a `configs/training/*.toml` file and run `python -m qminiwasm.engine --config …` from the **repository root** (editable `qminiwasm` package and `.env` on `PYTHONPATH`).

## Requirements

- [Go](https://go.dev/dl/) 1.22+
- Python env with the package installed (`pip install -e ".[training]"`) and `python` on `PATH`
- **RunPod (optional):** [OpenTofu](https://opentofu.org/docs/intro/install/) **`tofu`** or HashiCorp **Terraform** on `PATH` (the server shells out to `tofu` / `terraform` under `infra/runpod`). Setup checklist: [docs/RUNPOD_QUICKSTART.md](../docs/RUNPOD_QUICKSTART.md).

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

From the **repository root**, you can use **[`scripts/deploy-wui.sh`](../scripts/deploy-wui.sh)** (`./scripts/deploy-wui.sh`) to build the same binary; set **`DEST=/path/to/training-wui`** to copy it after the build (optional). The script comments list **RunPod** env vars (`RUNPOD_TOKEN`, `RUNPOD_API_KEY`, **`RUNPOD_TOKEN_END`**, **`RUNPOD_SERVERLESS_ENDPOINT_ID`**) — see [`.env.example`](../.env.example) and [docs/RUNPOD_SERVERLESS.md](../docs/RUNPOD_SERVERLESS.md).

Open [http://127.0.0.1:8765](http://127.0.0.1:8765).

### Flags

| Flag | Default | Meaning |
|------|---------|---------|
| `-addr` | `:8765` | Listen address (`host:port`) |
| `-root` | `.` | **Repo root** (directory that contains `configs/` and `qminiwasm/`) |
| `-python` | `python` | Python executable name or path on `PATH` |

### Optional C++ gRPC telemetry bridge

To consume native C++ training telemetry (`StreamTelemetry`) while keeping the current browser WebSocket payload contract unchanged:

- Set `QMINIWASM_WUI_GRPC_BRIDGE=1`
- Optionally set `QMINIWASM_TRAINING_GRPC_ADDR` (default `127.0.0.1:50061`)
- Install `grpcurl` on `PATH`

When enabled, `training-wui` reads gRPC stream events and re-broadcasts them as existing `metric`/`alert` WebSocket messages, so frontend code does not need changes.

### Tabs (workflow)

- **Training** — Single **LEGO-style wizard** (five steps on one tab): (1) dataset mix, (2) model & runtime, (3) data source, (4) training knobs + full **schema** form, (5) review/save, **Launch training** (only control that starts `python -m qminiwasm.engine`), preflight, runs, artifacts. All saves target **`configs/training/wui_working.toml`** unless you pick another file in the dropdown.
- **Mission Control**, **Infra & RunPod** (node health, `terraform.tfvars`, RunPod status, OpenTofu), **Quantum Topology**, **Artifact Registry** — Mission Control holds quick metrics, live telemetry, and the training log moved off the wizard for headroom.

`POST /api/runs/build` and `POST /api/runs/custom` only write `wui_working.toml`; **`POST /api/runs`** starts training. Optional JSON field **`allow_missing_checkpoint`**: when `true` and the config’s `checkpoint.load_path` file is missing, the server runs from a temp copy of the TOML with that line removed (fresh weights). The UI sets this only after you confirm in the resume-without-file dialog.

Only one training process at a time is allowed (start another after the current run finishes or after **Stop**).

### RunPod (OpenTofu)

End-to-end setup: [docs/RUNPOD_QUICKSTART.md](../docs/RUNPOD_QUICKSTART.md) (API key in `.env`, `scripts/runpod_bootstrap.sh`).

The dashboard can target **RunPod** so OpenTofu runs **`apply`** before training and **`destroy`** when the job exits or you **Stop** (optional checkbox: skip destroy if you want to keep the pod).

- Set **`RUNPOD_TOKEN`** (or `RUNPOD_API_KEY`) in the repo **`.env`**; the WUI loads it on startup (`loadDotenvFromRepo`).
- Stack lives in **`infra/runpod`**. **`tofu` or `terraform` must be on `PATH`** on the **WUI host**.
- **Train on the pod (default):** In step 2, choose **RunPod** and leave **Train on RunPod GPU** checked. **Launch training** will: `tofu apply` (unless you check **Skip OpenTofu apply**), wait for **`public_ip`**, **tar-sync** the repo to the pod over **SSH**, then run **`python -m qminiwasm.engine`** on the pod (venv + `pip install -e ".[training]"` on each run). The WUI host must have **`ssh`**, **`tar`**, and **`scp`** (scp only needed when the server uses a generated temp config) on **`PATH`**. Optional **`RUNPOD_SSH_USER`**, **`RUNPOD_REMOTE_DIR`**, **`RUNPOD_SSH_KEY`** in `.env` (see [`.env.example`](../.env.example)). Your repo **`.env`** is included in the sync so Hub / IBM tokens work remotely.
- **Train on the WUI host:** Uncheck **Train on RunPod GPU** — the pod is still provisioned, but **`python -m qminiwasm.engine`** runs locally (legacy / CPU testing).
- **Artifacts:** Checkpoints are written on the **pod** under the synced repo (e.g. `artifacts/models/...`). Copy them back with **scp**/**rsync** if you need them on your laptop. **Stop** terminates the local **ssh** process; the remote Python process may keep running until the pod is destroyed or you SSH in manually.

**Cloud GPU (CUDA):** Pods default to **`ACCELERATOR=cuda`** in container env (`infra/runpod/variables.tf`). See **`infra/runpod/CLOUD_ACCELERATOR.md`**.

**Infra & RunPod tab:** edit **`infra/runpod/terraform.tfvars`**, **Status** (badges include **ssh** / **scp** / **tar**), and **OpenTofu CLI**. **`GET /api/meta`** includes **`runpod_remote`** defaults and tool detection.

**Floating panels (optional):** On **Training** and **Infra & RunPod**, enable **Floating panels** to drag sections by their **title** and resize from the **corner grip**. The **Log** panel lives on **Mission Control** when using the default layout. Layout is stored in **`localStorage`**.

### Agent / inference outputs (after Launch training)

Each training run writes checkpoints under **`artifacts/models/<model-slug>/`** (repo root, gitignored):

| File | Role |
|------|------|
| `final.pt` | End-of-run weights (`CHECKPOINT_SAVE_PATH`) |
| `best.pt` | Best training MSE so far (`CHECKPOINT_BEST_PATH`) — **default for serving** |
| `latest.pt` | Last epoch (`CHECKPOINT_LATEST_PATH`) — used for **resume** |
| `serve.toml` | Minimal `[serve]` table; load with **`QMINIWASM_SERVE_CONFIG=artifacts/models/<slug>/serve.toml`** |
| `agent_bundle.json` | Machine-readable paths + **`uvicorn qminiwasm.engine.serve:app`** hint + HTTP API summary |

After training, point tools or agents at **`agent_bundle.json`** or set **`QMINIWASM_CHECKPOINT=artifacts/models/<slug>/best.pt`** and run **`uvicorn qminiwasm.engine.serve:app`** (**`pip install -e ".[serve]"`**). Inference is **`POST /infer`** with **`hidden_states`** (batch of 4096-float vectors); see the bundle JSON for the exact contract.

`GET /api/runpod/status` — token, binary, `tofu output` (when state exists), plus **`remote_ssh`**, **`remote_scp`**, **`remote_tar`**, **`remote_ssh_user`**, **`remote_dir`**, **`remote_ssh_key_set`**. `GET` / `PUT /api/runpod/tfvars` — read or write **`terraform.tfvars`** only (`PUT` body `{ "content": "…" }`; `GET ?source=example` returns the example file). `POST /api/runpod/tofu` — JSON `{ "action": "init"|"plan"|"apply"|"destroy", "var_file": "terraform.tfvars" }` (`var_file` optional).

**RunPod Serverless:** Queue calls use **`RUNPOD_TOKEN_END`** (endpoint API key) when set, else **`RUNPOD_API_KEY`** / **`RUNPOD_TOKEN`**. Management (`rest.runpod.io`: templates + endpoints) uses the account key only. Routes: `GET /api/runpod/serverless/meta`, `GET /api/runpod/serverless/worker-image?config=…`, `GET` / `POST /api/runpod/serverless/templates` (**`POST` creates templates via GraphQL `saveTemplate` on api.runpod.io**), `GET` / `POST /api/runpod/serverless/endpoints`, `GET /api/runpod/serverless/health`, `POST /api/runpod/serverless/run`, `GET /api/runpod/serverless/job?id=…`. See **[docs/RUNPOD_SERVERLESS.md](../docs/RUNPOD_SERVERLESS.md)**. **`GET /api/meta`** includes **`runpod_serverless`** flags (`endpoint_key_present`, `management_key_present`).

**`POST /api/runs`** accepts `run_target`: `"local"` | `"runpod"` | **`"runpod_serverless"`**; for pods: `runpod_destroy_on_exit`, optional **`runpod_var_file`**, **`runpod_train_on_pod`** (default **true** when `runpod`), **`runpod_skip_apply`**. For serverless: optional **`runpod_serverless_endpoint_id`** (else `RUNPOD_SERVERLESS_ENDPOINT_ID` in `.env`).

**Preflight FAQ**

- **CPU vs Build wizard accelerator**: Preflight loads the TOML from the **config dropdown** (e.g. `cascade_mopd.toml` has `accelerator = "cpu"`). The **Build wizard** accelerator is sent as `?accelerator=` so device resolution matches what you intend to train with, without editing that file.
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
