# Q-Mini-WASM: Ternary Network Execution Framework

A framework for training and executing BitNet-style ternary quantized neural networks within a WebAssembly runtime environment.

## What It Is

This framework provides a pipeline for converting neural network weights to ternary values (`{-1, 0, 1}`) and executing them in a WASM-based environment. The core components are:

- **Ternary Quantization**: Forces continuous weights into strict `{-1, 0, 1}` states using a Straight-Through Estimator (STE) during training
- **WASM Execution Engine**: Compiles and executes WASM modules with deterministic stack mechanics
- **Quantum-Classical Hybrid Routing**: Optional quantum-accelerated routing for model inference (defaults to local PennyLane simulator)

The ternary quantization approach reduces model size and enables deterministic execution, making it suitable for resource-constrained edge environments.

## How Ternary Quantization Works

The `TernaryWASMExpert` layer implements ternary quantization:

1. **Adaptive Thresholding**: Weights are normalized by their absolute mean
2. **Discretization**: Values are rounded to the nearest integer and clamped to `[-1, 1]`
3. **Straight-Through Estimator**: During backpropagation, gradients flow through the continuous weights while forward passes use discrete ternary values

This maintains differentiability during training while ensuring deterministic execution at inference time.

## Pipeline Overview

```
Training Data → TernaryWASMExpert → WASM Compilation → Execution
```

1. **Training**: Model weights are quantized to ternary values using STE
2. **Compilation**: WASM modules are compiled from C source or bytecode
3. **Execution**: The WASM engine runs the compiled modules with captured stack/memory state

## Prerequisites

- Python 3.8+
- PyTorch 2.0+
- `clang` (for C-to-WASM compilation) or pre-compiled WASM modules
- PennyLane (for quantum routing, optional)

## Installation

```bash
# Clone the repository
git clone <repository-url>
cd qminiwasm-core

# Install dependencies (includes PennyLane for local simulators)
pip install -e .

# Optional: FastAPI inference server (engine/serve.py)
pip install -e ".[serve]"

# Optional: Intel ARC (XPU) / dpctl
pip install -e ".[arc]"
```

## Basic Usage

### Training a Ternary Model

```python
from qminiwasm.model import QMiniWASM
import torch

# Initialize model (defaults to CPU or Intel ARC if available)
model = QMiniWASM()

# Prepare training data
hidden_states = torch.randn(32, 4096)  # batch_size=32, dim=4096
targets = torch.randn(32, 4096)

# Run training loop
from qminiwasm.training.loop import run_training_loop

results = run_training_loop(
    epochs=10,
    batch_size=32,
    learning_rate=1e-4,
    quantum_backend="penny_lane",  # or None to disable quantum routing
    num_qubits=8,
    qaoa_layers=3,
)
```

### Executing WASM Code

```python
from qminiwasm.model import QMiniWASM

model = QMiniWASM()

# Execute a WASM function
wasm_code = b"...your wasm bytecode..."
result, execution_state = model.execute_wasm(
    wasm_code=wasm_code,
    func_name="add",
    args=[5, 3]
)

print(f"Result: {result}")
print(f"Execution state: {execution_state}")
```

### Hybrid Inference

```python
# Run inference with quantum-classical hybrid routing
hidden_states = torch.randn(1, 4096)
output = model.hybrid_inference(hidden_states)
```

## Configuration

Configuration is loaded from environment variables or defaults:

- `ACCELERATOR`: `"cuda"`, `"xpu"`, or `"cpu"` (default: auto-detect)
- `QUANTUM_BACKEND`: `"penny_lane"` or `None` (default: `"penny_lane"`)
- `NUM_QUBITS`: Number of qubits for QAOA (default: `8`)
- `QAOA_LAYERS`: QAOA circuit depth (default: `3`)

## Project Structure

```
qminiwasm-core/
├── qminiwasm/
│   ├── layers/
│   │   └── ternary.py          # TernaryWASMExpert implementation
│   ├── wasm/
│   │   └── engine.py           # WASM compilation and execution
│   ├── quantum/
│   │   └── router.py           # Quantum routing (QAOA)
│   ├── model.py                # Main QMiniWASM interface
│   └── training/
│       └── loop.py             # Training loop
├── engine/                     # Training entrypoint
├── docs/                       # Documentation
└── requirements.txt
```

## License

MIT License
