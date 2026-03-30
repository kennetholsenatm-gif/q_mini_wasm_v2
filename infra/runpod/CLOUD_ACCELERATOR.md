# Cloud accelerators (Runpod): GPU-ready training

Setup checklist: [docs/RUNPOD_QUICKSTART.md](../../docs/RUNPOD_QUICKSTART.md).

The **training WUI** can perform this flow for you: **Infra & RunPod** for OpenTofu, then **Launch training** with **RunPod** + **Train on RunPod GPU** (default) to sync over SSH and run the **native** stack on the pod (**`qminiwasm_training_engine_server`** + **`go run ./cmd/qmw-grpc-train`**), not the legacy Python engine. See [`training-wui/runpod_remote.go`](../../training-wui/runpod_remote.go).

The Runpod pod uses a **CUDA PyTorch** image by default (`image_name` in `variables.tf`) and injects:

| Variable | Typical value | Purpose |
|----------|---------------|---------|
| `ACCELERATOR` | `cuda` | Matches `engine` / `get_device()` — same as local `.env` |
| `NVIDIA_VISIBLE_DEVICES` | `all` | Expose all GPUs to the container |

Training still **defaults to CPU** if you override `env` and drop `ACCELERATOR`, or if CUDA isn’t visible (wrong image / driver issue).

## After `tofu apply`

1. Read **`public_ip`** (Runpod UI or `tofu output -raw public_ip` from `infra/runpod`).
2. **Sync the repo** to the pod (paths vary by image; many templates use `/workspace`):

   ```bash
   export RUNPOD_IP="$(cd infra/runpod && tofu output -raw public_ip)"
   rsync -avz --exclude .git --exclude .venv --exclude __pycache__ \
     ./ root@${RUNPOD_IP}:/workspace/qminiwasm-core/
   ```

3. **SSH** (Runpod gives SSH key / user in the console; often `root` on community GPU templates):

   ```bash
   ssh root@${RUNPOD_IP}
   ```

4. On the pod, pick **one** path:

   **A — Native training (matches WUI “train on pod”):** Install **Go**, build **`qminiwasm_training_engine_server`** and LibTorch per [`cpp/training/README.md`](../../cpp/training/README.md), set `ACCELERATOR=cuda` and `LD_LIBRARY_PATH` to LibTorch `lib` if needed, then:

   ```bash
   cd /workspace/qminiwasm-core
   export ACCELERATOR=cuda
   ./build/cpp/training/qminiwasm_training_engine_server 127.0.0.1:50061 &   # or Release path — see runpod_remote.go search paths
   sleep 2
   ROOT="$(pwd)"
   (cd training-wui && go run ./cmd/qmw-grpc-train -root "$ROOT" -config configs/training/mesh_cuda.toml -grpc 127.0.0.1:50061)
   ```

   Adjust the server binary path to match your CMake output directory.

   **B — Legacy Python engine (optional, not WUI default):** ad hoc supervised loop only:

   ```bash
   cd /workspace/qminiwasm-core
   python -m venv .venv && source .venv/bin/activate
   pip install -U pip
   pip install -e ".[training]"
   export ACCELERATOR=cuda
   python -c "import torch; print('cuda:', torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else '')"
   python -m qminiwasm.engine --config configs/training/mesh_cuda.toml
   ```

   Use a config whose `[hardware]` section sets `accelerator = "cuda"` **or** rely on `ACCELERATOR` in the environment (see `qminiwasm/engine/config.py`).

## Training WUI

With **RunPod** + **Train on cloud GPU**, the WUI runs **native** training **on the pod** over SSH (see above). If you **uncheck** train on pod, the WUI runs **gRPC training on the machine hosting the WUI** (local C++ engine), while the pod may still be provisioned. For GPU-native training, either use the default (train on pod) or run the WUI **inside** the pod and use the **local** target so the engine and GPU share the same host.

## See also

- [README.md](README.md) — apply/destroy, token, state
- [../../docs/planning/RUNPOD_WUI.md](../../docs/planning/RUNPOD_WUI.md) — roadmap for deeper integration
