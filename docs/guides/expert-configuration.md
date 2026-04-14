# Expert Configuration Guide

## Overview

This guide provides configuration recommendations for all supported MoE scales: 16, 64, 243, and 8192 experts.  
**Current production-tested:** 243 experts  
**Architecture target:** 8192 experts

## Supported Scales

| Scale | Total Experts | Active (Top-K) | Topology | Memory (Base) | Status |
|-------|--------------|----------------|----------|---------------|--------|
| Small | 16 | 4 | RING | ~100 KB | Tested |
| Medium | 64 | 8 | SMALL_WORLD | ~400 KB | Tested |
| Production | 243 | 16 | HIERARCHICAL | ~1.5 MB | Production |
| Target | 8192 | 64 | HIERARCHICAL + Betti | ~48 MB | Development |

## System Requirements

### 243-Expert Production

**Minimum:**
- **Memory**: 16 GB RAM (32 GB recommended)
- **Storage**: 10 GB (weights and checkpoints)
- **CPU**: 8 cores (16 threads)
- **GPU**: Optional (SYCL-compatible)

**Recommended:**
- **Memory**: 64 GB RAM
- **GPU**: Intel Arc / NVIDIA with 16 GB VRAM
- **Storage**: NVMe SSD with 50 GB free
- **Network**: 1 Gbps

### 8192-Expert Target

**Projected Requirements:**
- **Memory**: 256 GB RAM
- **Storage**: 200 GB
- **GPU**: Multi-GPU with 64+ GB VRAM
- **Network**: 10 Gbps (distributed)

## Quick Start

### 16-Expert Small Scale

```cpp
#include "core/moe/unified_config.hpp"

auto config = UnifiedMoEConfig::SmallScale();
// Total: 16, Active: 4, Topology: RING
```

### 64-Expert Medium Scale

```cpp
auto config = UnifiedMoEConfig::MediumScale();
// Total: 64, Active: 8, Topology: SMALL_WORLD
```

### 243-Expert Production Scale

```cpp
#include "core/moe/unified_router.hpp"

// Use pre-configured router
auto router = Create243ExpertRouter();
// Or manually:
auto config = UnifiedMoEConfig::LargeScale();
// Total: 243, Active: 16, Topology: HIERARCHICAL
```

### 8192-Expert Target Scale

```cpp
// Configure for target scale with Betti guidance
PipelineConfig config;
config.moe_num_experts = 8192;
config.moe_top_k = 64;
config.enable_betti_guidance = true;
config.betti_guidance_threshold = 15;
```

## Topology Selection

### Decision Tree

```
Number of Experts?
├── ≤ 32 → RING
│   └── Simple, fast, good locality
├── 33-128 → SMALL_WORLD
│   └── Balance of locality and connectivity
├── 129-1024 → HIERARCHICAL
│   └── Required for 243-expert scale
└── > 1024 → HIERARCHICAL + Betti
    └── Dynamic topology optimization for 8192
```

### Ring Topology (≤32 Experts)

```cpp
config.topology = UnifiedMoEConfig::TopologyType::RING;
config.use_hierarchical_selection = ternary::Trit::ZERO;
```

- O(n) edges (2 per expert)
- Fast message passing
- Best for small clusters

### Small-World Topology (33-128 Experts)

```cpp
config.topology = UnifiedMoEConfig::TopologyType::SMALL_WORLD;
config.small_world_rewiring_prob = 0.3;
config.small_world_k = 4;
config.use_hierarchical_selection = ternary::Trit::POSITIVE;
```

- O(n log n) average path length
- Sparse: O(n) edges
- Watts-Strogatz model

### Hierarchical Topology (129+ Experts)

```cpp
config.topology = UnifiedMoEConfig::TopologyType::HIERARCHICAL;
config.cluster_size = 16;
config.num_clusters = 16;  // 16×16 = 256 capacity
config.use_hierarchical_selection = ternary::Trit::POSITIVE;
```

For 8192 experts:
```cpp
// Hierarchical with Betti-guided optimization
config.cluster_size = 64;
config.num_clusters = 128;  // 64×128 = 8192
config.enable_betti_guidance = true;
```

## Memory Sizing

### Calculation Formula

```
Router Memory:
- Expert specializations: total_experts × spec_dim × 2 bits
- Entanglement edges: edges × 12 bytes (sparse)
- Routing weights: total_experts × routing_dim × 2 bits
- Cluster metadata: clusters × cluster_size × 4 bytes

Expert Networks (per expert):
- Weights: num_layers × input_dim × hidden_dim × 2 bits
- Bias: hidden_dim × 2 bits
```

### Scale Memory Table

| Experts | Router Memory | Expert Networks (3-layer) | Total Base |
|---------|---------------|----------------------------|------------|
| 16 | 2 KB | 96 KB | ~100 KB |
| 64 | 8 KB | 384 KB | ~400 KB |
| 243 | 27 KB | 1.4 MB | ~1.5 MB |
| 8192 | 896 KB | 48 MB | ~49 MB |

**Note:** Add ~20% overhead for buffers and alignment.

## Active Expert Selection (Top-K)

### Recommended K Values

| Total Experts | K | Sparsity | Notes |
|--------------|---|----------|-------|
| 16 | 4 | 25% | High overlap acceptable |
| 64 | 8 | 12.5% | Balanced |
| 243 | 16 | 6.6% | Production standard |
| 8192 | 64 | 0.78% | Ultra-sparse target |

### Configuration

```cpp
// Enable parallel Top-K selection
config.use_parallel_topk = true;
config.topk_batch_size = 64;
```

## Betti-Guided Topology (8192 Scale)

When scaling to 8192 experts, enable Betti-guided optimization:

```cpp
// From autonomous_training_pipeline.hpp
PipelineConfig config;
config.enable_betti_guidance = true;
config.betti_guidance_threshold = 15;  // β₁ threshold
config.betti_max_qutrits = 8192;
```

The system monitors:
- β₀: Connected components
- β₁: 1-cycles (loops) - triggers simplification when > 15
- β₂: 2-voids (cavities)

## Energy Budget Configuration

```cpp
enum class EnergyTrit {
    LOW,     // < 0.5 pJ per operation
    MEDIUM,  // 0.5-1.0 pJ per operation
    HIGH     // > 1.0 pJ per operation
};

// Edge device
config.energy_budget = ternary::EnergyTrit::LOW;
config.active_experts = 8;

// Data center
config.energy_budget = ternary::EnergyTrit::MEDIUM;
config.active_experts = 16;

// HPC
config.energy_budget = ternary::EnergyTrit::HIGH;
config.active_experts = 32;
```

## Production Deployment (243 Experts)

### Recommended Configuration

```cpp
auto config = UnifiedMoEConfig::LargeScale();

// Topology: Hierarchical for 243-expert scale
config.topology = UnifiedMoEConfig::TopologyType::HIERARCHICAL;
config.cluster_size = 16;
config.num_clusters = 16;

// Routing: Top-16 of 243
config.active_experts = 16;
config.use_hierarchical_selection = ternary::Trit::POSITIVE;

// Parallelization
config.use_parallel_topk = true;
config.topk_batch_size = 64;

// Load balancing
config.enable_load_balancing = ternary::Trit::POSITIVE;
config.load_balance_alpha_fixed = 100;  // 0.1

// Energy
config.energy_budget = ternary::EnergyTrit::MEDIUM;
```

## Migration Paths

### From 64 to 243 Experts

```cpp
// 1. Export 64-expert configuration
auto old_config = UnifiedMoEConfig::MediumScale();

// 2. Create 243-expert configuration
auto new_config = UnifiedMoEConfig::LargeScale();

// 3. Copy relevant settings
new_config.specialization_dim = old_config.specialization_dim;

// 4. Enable hierarchical routing
new_config.use_hierarchical_selection = ternary::Trit::POSITIVE;

// 5. Create new router
auto router = CreateUnifiedRouter(new_config);
```

### From 243 to 8192 Experts

```cpp
// Transition to AutonomousTrainingPipeline for Betti guidance
PipelineConfig config;
config.moe_num_experts = 8192;
config.moe_top_k = 64;
config.enable_betti_guidance = true;
config.enable_knowledge_engine = true;

AutonomousTrainingPipeline pipeline;
pipeline.initialize(config);
```

## Troubleshooting

### High Memory Usage

**Symptoms:** OOM errors, swapping

**Solutions:**
1. Check topology (should be HIERARCHICAL, not DENSE)
2. Reduce specialization_dim (try 64 instead of 128)
3. Reduce active_experts (try 8 instead of 16)

### Slow Routing (>200μs)

**Symptoms:** High latency, poor throughput

**Solutions:**
1. Enable hierarchical_selection
2. Enable parallel_topk
3. Reduce cluster_size
4. Use SYCL acceleration

### No Expert Specialization

**Symptoms:** Uniform utilization, low model quality

**Solutions:**
1. Reduce load_balance_alpha (try 0.001)
2. Use DataSynthesizer for diverse training data
3. Check Forward-Forward training is working

## See Also

- [Expert Network Architecture](../architecture/expert-networks.md) - Architecture details
- [MoE Training Guide](moe-training.md) - Training procedures
- [Autonomous Training Pipeline](autonomous-pipeline.md) - Continuous training
- [Data Synthesizer](data-synthesizer.md) - Data acquisition

---

*Version: 2.0*  
*Last Updated: April 2026*  
*Source: `q_mini_wasm_v2/core/moe/unified_config.hpp`*
