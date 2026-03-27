# Install and configure PyTorch XPU (Intel GPU)

Training and `hybrid_inference` use **`torch.nn`** on whatever device `get_device()` picks. **Intel Iris Xe / Arc** only participate when PyTorch exposes them as **`torch.device("xpu")`**, which requires the **XPU build of PyTorch** from the official PyTorch wheel index (or a compatible IPEX stack). **dpctl/SYCL** (used by `SYCLHardware`) is separate and does not replace this step.

## Prerequisites

- **Intel GPU driver** (Windows: Intel Graphics / Arc driver from Intel; Linux: distro packages or Intel repos).
- **Python 3.10–3.14** (match [PyTorch XPU](https://pytorch.org/get-started/locally/) support for your chosen release).
- Use a **virtual environment** for this project so XPU wheels do not fight a global CPU-only `torch`.

## 1. Install XPU-enabled PyTorch (recommended: stable XPU index)

From the repo root, with your venv activated:

```bash
python -m pip uninstall -y torch torchvision torchaudio
python -m pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/xpu
```

**Nightly** (if stable wheels are missing for your Python/OS):

```bash
python -m pip uninstall -y torch torchvision torchaudio
python -m pip install --pre torch torchvision torchaudio --index-url https://download.pytorch.org/whl/nightly/xpu
```

Or run the helper (same commands):

```bash
python scripts/install_torch_xpu.py
python scripts/install_torch_xpu.py --nightly
```

Then reinstall this project so dependencies resolve:

```bash
pip install -e ".[training]"
```

## 2. Verify `torch.xpu`

```bash
python scripts/verify_torch_xpu.py
```

You should see `torch.xpu.is_available(): True` and a device name. If it is **False**, check drivers, Python version, and that no **CPU-only** `torch` remains (`pip show torch` should show a build tied to the XPU index).

**Note:** PyTorch may print a **UserWarning** that integrated **Iris Xe** is not in the “officially supported” GPU list (Arc / newer discrete GPUs are the supported tier). Training can still run on Iris Xe; treat results as best-effort if you see that warning.

## 3. Configure the engine / serving

Prefer **`[hardware]`** in your training TOML (`accelerator`, `device_index` — see [`configs/training/schema.toml`](../configs/training/schema.toml)). For containers or CI that pin the device without editing the file, **`ACCELERATOR`** / **`DEVICE_INDEX`** may still be set in the shell; see **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)**.

```text
ACCELERATOR=xpu
DEVICE_INDEX=0
```

Optional (SYCL / Level Zero, for `SYCLHardware` — not required for `torch.xpu` itself):

```text
ONEAPI_DEVICE_SELECTOR=level_zero:gpu
```

Run training:

```bash
python -m qminiwasm.engine --config configs/training/mesh_cpu.toml
```

Logs should show **Intel XPU** (not “XPU requested but … using CPU”).

## 4. Alternative: Intel Extension for PyTorch (IPEX)

Intel documents wheels and retirement timelines at [Intel Extension for PyTorch](https://intel.github.io/intel-extension-for-pytorch/) and the [installation selector](https://pytorch-extension.intel.com/installation). Use IPEX only if you follow their pinned `torch` versions; mixing random `pip install torch` with IPEX often breaks.

## References

- [PyTorch Get Started](https://pytorch.org/get-started/locally/) — choose **XPU** as compute platform when available.
- [XPU wheel directory](https://download.pytorch.org/whl/xpu)
