"""Q-Mini-WASM: Hybrid Quantum-Classical Mixture-of-Experts Architecture

This package implements the Q-Mini-WASM architecture, a highly specialized 500-million
parameter prototype that operates at the absolute frontier of quantum machine learning (QML),
discrete computational geometry, and low-level hardware orchestration. It explicitly
internalizes exact computational execution by compiling a WebAssembly (WASM) interpreter
directly into specific latent weights of a Sparse Mixture-of-Experts (MoE) framework.

The architecture consists of five pillars:
1. Quantum MoE Router - Reformulates MoE routing as a discrete combinatorial optimization problem
2. Ternary Quantization - Forces WASM execution expert weights into an unstructured ternary state
3. Geometric HullKVCache - Implements 2D Tropical Attention with O(log N) convex-hull queries
4. Intel ARC/SYCL Hardware Mapping - Close-to-metal execution on Intel ARC GPUs
5. Synthetic Data Pipeline - Employs Wasmtime instrumentation and intentional fault injection

Main Classes:
- QMiniWASM: Top-level model interface
- HybridQuantumMoE: Quantum MoE router implementation
- TernaryWASMExpert: Ternary quantization expert
- WasmExecutor: WASM execution engine
"""

from .model import QMiniWASM

__version__ = "0.1.0"
__all__ = ["QMiniWASM"]
