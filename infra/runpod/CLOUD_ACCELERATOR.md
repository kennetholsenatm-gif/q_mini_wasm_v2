# Cloud accelerators (Runpod): GPU-ready training

Setup checklist: [docs/RUNPOD_QUICKSTART.md](../../docs/RUNPOD_QUICKSTART.md).

The **training WUI** can perform this flow for you: **Infra & RunPod** for OpenTofu, then **Launch training** with **RunPod** + **Train on RunPod GPU** (default) to sync over SSH and run `python -m engine` on the pod.

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

4. On the pod:

   ```bash
   cd /workspace/qminiwasm-core
   python -m venv .venv && source .venv/bin/activate
   pip install -U pip
   pip install -e ".[training]"
   export ACCELERATOR=cuda
   python -c "import torch; print('cuda:', torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else '')"
   python -m engine --config configs/training/mesh_cuda.toml
   ```

   Use a config whose `[hardware]` section sets `accelerator = "cuda"` **or** rely on `ACCELERATOR` in the environment (see `engine/config.py`).

## Training WUI

The dashboard can **provision** the pod (`run_target: runpod`) but still runs `python -m engine` on the **machine hosting the WUI**. For GPU training, run the WUI on your laptop and execute the engine **on the pod** via SSH/rsync as above, or run the WUI **inside** the same pod (then local training uses that pod’s GPU).

## See also

- [README.md](README.md) — apply/destroy, token, state
- [../../docs/planning/RUNPOD_WUI.md](../../docs/planning/RUNPOD_WUI.md) — roadmap for deeper integration
