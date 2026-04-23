# q_mini_wasm_v2 Documentation

Welcome to the documentation for **q_mini_wasm_v2**, a next-generation extreme-edge AI framework based on quantum-inspired computing, cognitive ergonomics, and ternary neural networks.

## Overview

q_mini_wasm_v2 achieves unprecedented energy efficiency by operating entirely in a 1.58-bit ternary state space `{+1, 0, -1}`, eliminating floating-point arithmetic while maintaining high expressivity through combinatorial routing and **QGNN graph-native data structures**.

## Implementation Purity Achievement

The repository has achieved 100% implementation purity through a comprehensive three-phase roadmap:

### Phase 1: Binary Pollution Elimination
- Complete elimination of `double`/`float` usage in critical components
- Ternary `EnergyTrit`, `ProbTrit`, and `Trit` type system
- Deterministic ternary random generation
- GF(3) arithmetic throughout

### Phase 2: GF(3) Validation Tooling
- Automated binary pollution detection
- Energy efficiency regression testing
- CI/CD quality gates with Gottesman-Knill validation
- Pre-commit developer validation

### Phase 3: QGNN Graph-Native Data Structures
- O(N²) arrays replaced with O(E) sparse graphs
- Quantum message passing architecture
- Scalable expert selection with graph attention
- Migration adapter for seamless transition

## New QGNN Capabilities

### Graph-Native Expert Routing
```cpp
#include "q_mini_wasm_v2/core/qgnn/graph_moe_router.hpp"

// Create graph-based MoE router
qgnn::GraphMoERouter::GraphConfig config{
    .max_experts = 243,
    .active_experts = 8,
    .specialization_dim = 16,
    .energy_budget = ternary::EnergyTrit::LOW
};
auto router = qgnn::create_graph_moe_router(config);

// Quantum message passing routing
auto result = router->route_quantum_graph(input, 8);
```

### Scalable Graph Structures
```cpp
// Graph-native expert management
auto graph = qgnn::create_qgnn_graph();
NodeID expert = graph->add_expert_node();
graph->add_entanglement_edge(expert1, expert2, ternary::Trit::POSITIVE);
```

### Migration Adapter
```cpp
// Seamless transition from arrays to graphs
qgnn::GraphMigrationAdapter::MigrationConfig migration{
    .use_graph_routing = false,
    .migration_ratio = 0.5,
    .enable_performance_logging = true
};
auto adapter = qgnn::create_migration_adapter(legacy_config, graph_config, migration);
```

## Documentation Structure

This documentation is organized into the following sections:

### API Reference
Complete API documentation for all framework components:
- Core ternary operations and GF(3) arithmetic
- Stabilizer tableau operations
- QGNN graph-native data structures
- Graph-based MoE routing
- Migration adapter system
- MoE routing with tropical geometry
- Forward-Forward learning algorithms
- Runtime orchestration

### Architecture
Detailed architecture documentation:
- Overview - System design and data flow
- Ternary State Space - GF(3) arithmetic and trit encoding
- Stabilizer Tableau - Qutrit Clifford gates
- MoE Routing - Tropical geometry expert routing
- QGNN Architecture - Graph-native quantum neural networks
- Graph Migration - Array-to-graph transition system
- Forward-Forward - Teacherless learning algorithm
- SYCL Acceleration - GPU/CPU parallelism

### Guides
Step-by-step guides for using the framework:
- [Quick Start](guides/quick-start.md) - Get started in minutes
- [Building](guides/building.md) - Build instructions for all platforms
- [Expert Configuration](guides/expert-configuration.md) - 16/64/243/8192 expert setup
- [MoE Training](guides/moe-training.md) - Training modes and CLI options
- [Autonomous Training Pipeline](guides/autonomous-pipeline.md) - Continuous training mode
- [Data Synthesizer](guides/data-synthesizer.md) - 15 knowledge engine APIs
- [SYCL Setup](guides/sycl-setup.md) - GPU acceleration setup
- [Contributing](guides/contributing.md) - Development workflow

### Decisions
Architecture Decision Records (ADRs):
- ADR-001: Ternary Over Binary - Why ternary state space

### Research
Foundational research documents:
- Cognitive Ergonomics Model Protocol
- Clifford Entanglement Framework
- Quantum-Classical Framework Synthesis

### Wiki Pipeline
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

#### Legacy Array-Based MoE (Still Supported)
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

#### New QGNN Graph-Native MoE (Recommended)
```cpp
#include "q_mini_wasm_v2/core/qgnn/graph_moe_router.hpp"
#include "q_mini_wasm_v2/core/qgnn/graph_migration_adapter.hpp"

using namespace q_mini_wasm_v2;

int main() {
    // Configure graph-based MoE router
    qgnn::GraphMoERouter::GraphConfig config{
        .max_experts = 243,
        .active_experts = 8,
        .specialization_dim = 16,
        .energy_budget = ternary::EnergyTrit::LOW
    };
    
    auto graph_router = qgnn::create_graph_moe_router(config);
    
    // Route with quantum message passing
    std::vector<ternary::Trit> input(16, ternary::Trit::POSITIVE);
    auto result = graph_router->route_quantum_graph(input, 8);
    
    // Or use migration adapter for seamless transition
    qgnn::GraphMigrationAdapter::MigrationConfig migration{
        .use_graph_routing = false,
        .migration_ratio = 0.5
    };
    
    auto adapter = qgnn::create_migration_adapter(
        legacy_config, graph_config, migration);
    auto unified_result = adapter->route_unified(input, 8);
    
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

### 3. QGNN Graph-Native Architecture
- O(E) Complexity: Sparse graphs replace O(N²) arrays
- Quantum Message Passing: Stabilizer-based information propagation
- Graph Attention: Ternary attention over graph structure
- Scalable Expert Selection: Linear scaling with graph size

### 4. Migration Adapter System
- Unified Interface: Seamless array-to-graph transition
- Performance Monitoring: Real-time comparison and validation
- Gradual Migration: Controlled progression with rollback capability

### 5. Implementation Purity Validation
- GF(3) Compliance: Automated binary pollution detection
- Energy Regression Testing: Continuous efficiency monitoring
- CI/CD Quality Gates: Pre-commit and continuous integration validation

### 6. Tropical Geometry MoE Routing
- Max-plus semiring: Tropical addition (max) and multiplication (add)
- Combinatorial depth: Hypersimplex capacity = C(n,k) regions
- Sparsity is expressivity: Sparse routing achieves high capacity

### 7. Forward-Forward Learning
- Teacherless SSL: No backpropagation, local layer-wise learning
- Tropical inner product: Goodness metric via max-plus algebra
- Hebbian updates: Gradient-free weight modifications
- **Two Training Modes:**
  - Epoch-based: Fixed-duration with pre-loaded dataset
  - Continuous (`--continuous`): Indefinite with live API data
- **15 Knowledge Engine APIs**: OpenAlex, arXiv, PubChem, NASA, GitHub, etc.
- **Betti-Guided Topology**: Dynamic optimization via algebraic topology (β₀, β₁, β₂)

### 8. SYCL Multi-Core Acceleration
- Parallel tableau updates: Distribute 2n×2n matrix operations
- Vectorized modulo-3: Sub-group parallelism for trit arithmetic
- Hardware agnostic: GPU/CPU acceleration via SYCL

## Documentation Standards

This documentation follows Cognitive Ergonomics principles:
- Line length less than or equal to 75 characters
- Paragraphs less than or equal to 4 lines
- Headers every 200 words
- Code blocks less than or equal to 15 lines
- Navigation depth less than or equal to 3 levels

## Contributing

See the [Contributing Guide](guides/contributing.md) for development workflow and standards.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

