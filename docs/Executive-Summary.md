# Executive Summary

## Overview

Q-Mini-WASM is a framework for training and executing ternary quantized neural networks in a WebAssembly runtime. The system converts continuous neural network weights to ternary values (`{-1, 0, 1}`) and executes them deterministically within WASM modules.

## Core Components

### Ternary Quantization

The framework implements BitNet-style ternary quantization using a Straight-Through Estimator (STE). During training, weights are discretized to `{-1, 0, 1}` while maintaining gradient flow through continuous parameters. This enables:

- Reduced model size (ternary weights vs. float32)
- Deterministic execution at inference time
- Compatibility with resource-constrained edge environments

### WASM Execution Engine

The WASM engine compiles C source to WebAssembly and executes modules with full stack and memory state capture. This provides:

- Deterministic execution mechanics
- Sandboxed runtime environment
- Cross-platform portability

### Quantum-Classical Hybrid Routing

Optional quantum-accelerated routing using the Quantum Approximate Optimization Algorithm (QAOA). Defaults to local PennyLane simulators; no cloud quantum hardware dependencies.

## Technical Architecture

The system follows a simple pipeline:

1. **Training**: Ternary quantization applied during training via `TernaryWASMExpert` layers
2. **Compilation**: WASM modules compiled from C source or bytecode
3. **Execution**: WASM engine runs modules with state capture
4. **Inference**: Optional quantum routing for hybrid quantum-classical inference

## Use Cases

- Edge AI deployments requiring deterministic execution
- Resource-constrained environments with limited memory
- Applications benefiting from ternary quantization (reduced model size)
- Research into quantum-classical hybrid neural networks

## Current Status

Alpha release. Core functionality is implemented:

- Ternary quantization with STE
- WASM compilation and execution
- Local quantum simulator integration
- Basic training loop

## Dependencies

- PyTorch 2.0+ for neural network operations
- PennyLane for quantum routing (optional)
- wasmtime for WASM execution
- clang for C-to-WASM compilation

## License

MIT License
