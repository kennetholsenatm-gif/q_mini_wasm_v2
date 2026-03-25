# Intel Quantum Computing and Intel ARC (XPU) Support

Q-Mini-WASM integrates with [Intel's quantum computing research and software stack](https://www.intel.com/content/www/us/en/research/quantum-computing.html) and uses **Intel ARC** (via Intel XPU) for AI training instead of CUDA.

## Intel Quantum Computing

Intel is working toward **quantum practicality**: moving quantum technology from the lab to commercial systems. Key elements supported or referenced by this project:

### Intel Quantum SDK

- **Overview**: [Intel Quantum SDK](https://www.intel.com/content/www/us/en/developer/tools/quantum-sdk/overview.html) – full-stack quantum computing in simulation.
- **API docs**: [Intel Quantum SDK API](https://intel.github.io/quantum-sdk-docs/) – Python API, tutorials, and backend options.
- **Backends**: Intel Quantum Simulator (IQS) and Silicon Spin Qubit Simulator for algorithm development and testing.

### Tunnel Falls and Silicon Spin Qubits

- **Tunnel Falls** is Intel’s most advanced silicon spin qubit chip and is available to the research community.
- Built with CMOS-style fabrication for scalability and reliability.
- The project’s quantum layer can be used alongside or in concert with Intel’s silicon spin qubit roadmap (simulation today; hardware when available).

### Intel Quantum Simulator (IQS)

- **Docs**: [Intel QS documentation](https://intel-qs.readthedocs.io/).
- **Repo**: [intel/intel-qs](https://github.com/intel/intel-qs) – high-performance state-vector simulator with optional Python bindings and MPI for distributed runs.
- When Intel QS (or Intel Quantum SDK) is installed, use `qminiwasm.fabric.intel_backend.get_intel_quantum_info()` and `get_intel_quantum_simulator_backend()` to detect and use these backends.

### Horse Ridge and Cryoprober

- **Horse Ridge** cryogenic control and **cryoprober** for high-volume testing are part of Intel’s path to scalable quantum systems. This project focuses on algorithm and software integration; control hardware is documented in Intel’s resources above.

## Intel ARC (XPU) for AI Training

AI training in Q-Mini-WASM targets **Intel ARC** (and other Intel GPUs) via the **Intel XPU** device, not CUDA.

### Device selection

- **`qminiwasm.hardware.device.get_device()`** – returns `torch.device("xpu:0")` when an Intel GPU (e.g. Intel ARC) and drivers are available, otherwise `torch.device("cpu")`.
- The main model (`QMiniWASM`) uses this device by default: quantum router, ternary expert, and inference/training run on XPU when available.

### Requirements

- **PyTorch with XPU**: Install PyTorch built with Intel XPU support (see [PyTorch XPU docs](https://pytorch.org/docs/stable/notes/get_start_xpu.html)).
- **Optional**: [Intel Extension for PyTorch (IPEX)](https://intel.github.io/intel-extension-for-pytorch/) for older PyTorch or extra Intel GPU optimizations. Newer PyTorch has XPU support upstream.
- **Drivers**: Install Intel GPU drivers and oneAPI runtime where required for your OS.

### Environment variables

- `INTEL_GPU_DEVICE` – device index (default `0`).
- `INTEL_GPU_MEMORY`, `INTEL_GPU_THREADS` – optional tuning.
- `INTEL_CLOUD_*` – for Intel cloud/ARC endpoints when used.

### SYCL / oneAPI

- The **SYCL** hardware layer (`qminiwasm.hardware.SYCLHardware`) provides the interface for close-to-metal execution on Intel ARC (Vector Engine XVE, Matrix Engine XMX, ternary packing). Install `requirements/hardware.txt` for SYCL/oneAPI Python bindings when building the full stack.

## Quick reference

| Resource            | URL |
|---------------------|-----|
| Intel quantum research | https://www.intel.com/content/www/us/en/research/quantum-computing.html |
| Intel Quantum SDK   | https://www.intel.com/content/www/us/en/developer/tools/quantum-sdk/overview.html |
| Intel Quantum SDK API | https://intel.github.io/quantum-sdk-docs/ |
| Intel QS (simulator) | https://intel-qs.readthedocs.io/ |
| PyTorch on Intel GPU | https://pytorch.org/docs/stable/notes/get_start_xpu.html |
| Intel Extension for PyTorch | https://intel.github.io/intel-extension-for-pytorch/ |

---

**Last updated**: 2026-03-12
