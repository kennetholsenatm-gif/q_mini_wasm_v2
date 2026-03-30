# RunPod integration in the training WUI (plan)

## Current state (implemented)

- **Execution target** in the UI: `local` | `runpod` (and serverless variants documented elsewhere).
- `runpod` runs **OpenTofu** `apply`/`destroy` under `infra/runpod` (token: `RUNPOD_API_KEY` / `RUNPOD_TOKEN`).
- **Train on pod (default):** after sync, the WUI runs a remote bash script that starts **`qminiwasm_training_engine_server`** and **`go run ./cmd/qmw-grpc-train`** (`training-wui/runpod_remote.go`). **Train on WUI host:** local gRPC to a C++ engine on the laptop; pod may still be provisioned.
- **Cloud accelerator defaults:** `infra/runpod/variables.tf` sets container **`ACCELERATOR=cuda`** and **`NVIDIA_VISIBLE_DEVICES=all`**. See **`infra/runpod/CLOUD_ACCELERATOR.md`**, **`scripts/runpod_sync_and_train.example.sh`**, and the WUI RunPod panel.

**Historical note:** earlier revisions assumed `python -m qminiwasm.engine` as the WUI subprocess; that is **legacy** and not the default training path ([docs/TRAINING_NATIVE_PARITY.md](../TRAINING_NATIVE_PARITY.md)).

## Gaps (what “full” RunPod integration could mean)

1. **Remote training** – Further automation for artifacts, health checks, and optional RunPod API exec/logs (native stack is already SSH-driven on the pod).
2. **Env sync** – Push `.env` / secrets to the pod (or use RunPod env vars / secrets store) so `IBM_QUANTUM_*`, `HF_*`, and TOML paths match.
3. **Artifacts** – Stream or copy **`artifacts/models/<slug>/`** (checkpoints, `serve.toml`, `agent_bundle.json`) back to the operator machine after the run (or write to S3 / volume).
4. **Observability** – Surface pod `public_ip`, SSH command, `tofu output` (already partially via `/api/runpod/status`), and RunPod job/pod IDs in the run log.
5. **Health** – Before `apply`, verify token, `tofu`/`tofu` binary, and `infra/runpod` layout; optional “dry run” `tofu plan`.

## Suggested phases

| Phase | Scope |
|-------|--------|
| **P0** | ~~Document current flow; optional “copy SSH command” from `tofu output` in the UI.~~ **Done:** `CLOUD_ACCELERATOR.md`, example script, WUI hints when `public_ip` is present; pod env defaults for CUDA. |
| **P1** | Optional **remote command** field: run custom `ssh user@ip '…'` (e.g. native `qmw-grpc-train` or legacy `python -m qminiwasm.engine`) after apply, with SSH key from env. |
| **P2** | **rsync** or small agent to sync repo + `configs/` + `artifacts/` bidirectionally. |
| **P3** | RunPod **HTTP API** (if available) for pod exec/logs instead of raw SSH. |

## Open decisions

- Single source of truth for **repo path** on the pod (Docker image vs. git clone vs. volume).
- Whether the WUI should **block** “RunPod” until `tofu output` shows a healthy `public_ip`, or allow fire-and-forget apply.

## Related paths

- `training-wui/runpod.go` – apply/destroy/status.
- `training-wui/main.go` – `run_target`, `runpod_destroy_on_exit`.
- `infra/runpod/` – OpenTofu module.
