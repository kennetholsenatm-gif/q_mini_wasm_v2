# q_mini_wasm_v2

Quantum-Classical Hybrid Framework with Ternary Compute-in-Memory.

## Overview

A quantum-inspired computing framework implementing:

- **Ternary Logic**: Native GF(3) operations with {+1, 0, -1} states
- **Mixture of Experts**: Self-organizing topology with Betti-guided routing
- **Flash CIM**: Compute-in-memory using flash storage primitives
- **WASM Runtime**: Zero-copy Go integration with web deployment

## Quick Start

### Build Requirements

- CMake 3.16+
- C++17 compiler
- Windows: Visual Studio 2022 or WinLibs
- Optional: SYCL for GPU acceleration

### Build Commands

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Run Training

```bash
./q_mini_wasm_v2_trainer \
  --dataset datasets/general/data.jsonl \
  --epochs 100 \
  --batch-size 32768
```

## Architecture

```
┌─────────────────────────────────────────┐
│  Training Pipeline                      │
│  - Forward-Forward Algorithm            │
│  - Self-Organizing Expert Manager     │
│  - Betti-Guided Topology Updates      │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│  MoE Router                             │
│  - Entangled Probability Distributions│
│  - Tropical Geometry Routing          │
│  - Top-K Expert Selection               │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│  Execution Layer                        │
│  - Stabilizer Tableau Operations        │
│  - Flash CIM Acceleration               │
│  - SYCL GPU Kernels                     │
└─────────────────────────────────────────┘
```

## Core Components

### Self-Organizing Expert Manager

Dynamic expert creation based on topological complexity:

- **Initial Experts**: 8 seed experts
- **Split Threshold**: Betti β₁ > 5
- **Max Experts**: 100,000 (soft cap)
- **Topology**: Graph density maintained at 15%

See: `core/moe/self_organizing_expert.hpp`

### Forward-Forward Learner

Training without backpropagation:

- Positive/negative sample contrast
- Local goodness optimization
- No gradient chain dependencies

See: `core/learning/forward_forward.hpp`

### Stabilizer Tableau

Quantum-inspired state representation:

- GF(3) arithmetic operations
- Symplectic inner products
- Clifford gate operations

See: `core/stabilizer/tableau.hpp`

## Configuration

Key parameters in `config/training.toml`:

```toml
[training]
epochs = 100
batch_size = 32768
checkpoint_interval = 5000

[model]
num_layers = 64
neurons_per_layer = 2048
moe_experts = 512
context_window = 8192
```

See `config/README.md` for all options.

## Documentation

| Module | Description |
|:-------|:------------|
| [DLL API](dll/README.md) | C API for external integration |
| [Architecture](docs/architecture/entangled-moe-routing.md) | MoE routing design |
| [Flash CIM](core/flash_cim/README.md) | Compute-in-memory |
| [QGNN](core/qgnn/README_QGNN_SYCL_OPTIMIZATION.md) | Quantum GNN kernels |
| [Go Bridge](go/README_GO_WASM_BRIDGE.md) | WASM integration |
| [MCP Consolidation](core/agents/README_MCP_CONSOLIDATION.md) | Agent architecture |
| [Stabilizer RAG](core/flash_cim/README_STABILIZER_RAG.md) | RAG retrieval |
| [Gemini Improver](agents/gemini_improver/README.md) | Agent improvement |

## Advanced

For experimental features and research:

- Entangled MoE routing with stabilizer states
- Betti number topology analysis
- Native HTTP client (WinHTTP)
- Incremental checkpointing with resume

See individual module docs above.

## License

Research framework. See LICENSE for details.
