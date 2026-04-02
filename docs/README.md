# q_mini_wasm_v2 Documentation

Welcome to the documentation for **q_mini_wasm_v2**, a next-generation extreme-edge AI framework based on quantum-inspired computing, cognitive ergonomics, and ternary neural networks.

## Overview

q_mini_wasm_v2 achieves unprecedented energy efficiency by operating entirely in a 1.58-bit ternary state space `{+1, 0, -1}`, eliminating floating-point arithmetic while maintaining high expressivity through combinatorial routing.

## Documentation Structure

This documentation is organized into the following sections:

### 📚 [API Reference](api/core-reference.md)
Complete API documentation for all framework components:
- Core ternary operations and GF(3) arithmetic
- Stabilizer tableau operations
- MoE routing with tropical geometry
- Forward-Forward learning algorithms
- Runtime orchestration

### 🏗️ [Architecture](architecture/overview.md)
Detailed architecture documentation:
- [Overview](architecture/overview.md) - System design and data flow
- [Ternary State Space](architecture/ternary-state-space.md) - GF(3) arithmetic and trit encoding
- [Stabilizer Tableau](architecture/stabilizer-tableau.md) - Qutrit Clifford gates
- [MoE Routing](architecture/moe-routing.md) - Tropical geometry expert routing
- [Forward-Forward](architecture/forward-forward.md) - Teacherless learning algorithm
- [SYCL Acceleration](architecture/sycl-acceleration.md) - GPU/CPU parallelism

### 📖 [Guides](guides/building.md)
Step-by-step guides for using the framework:
- [Quick Start](guides/quick-start.md) - Get started in minutes
- [Building](guides/building.md) - Build instructions for all platforms
- [SYCL Setup](guides/sycl-setup.md) - GPU acceleration setup
- [Contributing](guides/contributing.md) - Development workflow

### 🎯 [Decisions](decisions/adr-001-ternary-over-binary.md)
Architecture Decision Records (ADRs):
- [ADR-001: Ternary Over Binary](decisions/adr-001-ternary-over-binary.md) - Why ternary state space

### 🔬 [Research](research/)
Foundational research documents:
- Cognitive Ergonomics Model Protocol
- Clifford Entanglement Framework
- Quantum-Classical Framework Synthesis

### 🛠️ [Wiki Pipeline](wiki-pipeline/)
Tools for generating GitHub Wiki documentation:
- `generate_wiki.py` - Converts docs to Wiki format
- `cognitive_linter.py` - Validates cognitive ergonomics compliance

## Quick Start

### Prerequisites
- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14+
- Git

### Basic Build

```bash
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2/q_mini_wasm_v2
mkdir build && cd build
cmake ..
cmake --build .
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

### Usage Example

```cpp
#include "q_mini_wasm_v2/core/ternary/trit.hpp"
#include "q_mini_wasm_v2/core/stabilizer/tableau.hpp"
#include "q_mini_wasm_v2/core/moe/router.hpp"
#include "q_mini_wasm_v2/runtime/orchestrator.hpp"

using namespace q_mini_wasm_v2;

int main() {
    // Create 4-qutrit stabilizer tableau
    auto tableau = core::stabilizer::create_tableau(4);
    
    // Apply Clifford gates
    tableau->apply_hadamard(0);
    tableau->apply_csum(0, 1);
    
    // Create MoE router with 8 experts, Top-2 routing
    core::moe::ExpertConfig config{8, 2, 4};
    auto router = core::moe::create_moe_router(config);
    
    // Route input through experts
    std::vector<core::ternary::Trit> input = {
        core::ternary::Trit::POSITIVE,
        core::ternary::Trit::ZERO,
        core::ternary::Trit::NEGATIVE,
        core::ternary::Trit::POSITIVE
    };
    
    auto experts = router->route_topk(input);
    return 0;
}
```

## Key Concepts

### 1. Ternary State Space (1.58-bit)
- Trit operations: `{+1, 0, -1}` mapped to qutrit computational basis
- GF(3) arithmetic: All operations over Galois Field of order 3
- 99.06% entropy efficiency: 5-trit-to-8-bit packing achieves near-Shannon limit

### 2. Gottesman-Knill Theorem Application
- O(n²) complexity: Stabilizer tableau tracks n qutrits efficiently
- Clifford gates: H (Hadamard), S (Phase), CSUM (Controlled-SUM)
- No exponential overhead: Classical simulation of quantum-inspired operations

### 3. Tropical Geometry MoE Routing
- Max-plus semiring: Tropical addition (max) and multiplication (add)
- Combinatorial depth: Hypersimplex capacity = C(n,k) regions
- Sparsity is expressivity: Sparse routing achieves high capacity

### 4. Forward-Forward Learning
- Teacherless SSL: No backpropagation, local layer-wise learning
- Tropical inner product: Goodness metric via max-plus algebra
- Hebbian updates: Gradient-free weight modifications

### 5. SYCL Multi-Core Acceleration
- Parallel tableau updates: Distribute 2n×2n matrix operations
- Vectorized modulo-3: Sub-group parallelism for trit arithmetic
- Hardware agnostic: GPU/CPU acceleration via SYCL

## Documentation Standards

This documentation follows Cognitive Ergonomics principles:
- Line length ≤ 75 characters
- Paragraphs ≤ 4 lines
- Headers every ±200 words
- Code blocks ≤ 15 lines
- Navigation depth ≤ 3 levels

## Contributing

See the [Contributing Guide](guides/contributing.md) for development workflow and standards.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

