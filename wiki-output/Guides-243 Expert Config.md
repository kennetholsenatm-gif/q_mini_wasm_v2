# 243-Expert Configuration Guide

## Overview

This guide provides specific configuration recommendations for deploying
243-expert Mixture of Experts (MoE) models at scale.

## System Requirements

### Minimum Requirements
- **Memory**: 16 GB RAM (32 GB recommended)
- **Storage**: 10 GB (for expert weights and checkpoints)
- **CPU**: 8 cores (16 threads)
- **GPU**: Optional but recommended (SYCL-compatible)

### Recommended for Production
- **Memory**: 64 GB RAM
- **GPU**: Intel Arc / NVIDIA with 16 GB VRAM
- **Storage**: NVMe SSD with 50 GB free
- **Network**: 1 Gbps (for distributed training)

## Quick Start

### Pre-configured 243-Expert Setup

```cpp
#include "core/moe/unified_config.hpp"
#include "core/moe/unified_router.hpp"

// Use the built-in large-scale configuration
auto config = UnifiedMoEConfig::LargeScale();

// Create pre-configured router
auto router = Create243ExpertRouter();

// Ready to use!
```

### Builder Pattern for Customization

```cpp
auto config = MoEConfigBuilder()
    .TotalExperts(243)
    .ActiveExperts(16)
    .SpecializationDim(128)
    .Topology(UnifiedMoEConfig::TopologyType::HIERARCHICAL)
    .HierarchicalParams(16, 16)  // 16 experts per cluster, 16 clusters
    .EnableLoadBalancing(true)
    .EnergyBudget(ternary::EnergyTrit::MEDIUM)
    .Build();
```

## Memory Sizing

### Base Memory Calculation

```
Experts:          243 × specialization_dim × 2 bits
Entanglement:     edges × (from + to + strength) × 4 bytes
Routing weights:  243 × routing_dim × 2 bits
Cluster metadata: clusters × cluster_size × 4 bytes
Load tracking:    243 × counters × 8 bytes
Overhead:         ~20% for buffers and alignment
```

### Example: 243 Experts with 128-dim Specialization

```
Expert specializations: 243 × 128 × 2 = 62,208 bits = 7.8 KB
Entanglement (small-world): ~1,000 edges × 12 bytes = 12 KB
Routing weights: 243 × 64 × 2 = 31,104 bits = 3.9 KB
Cluster metadata: 16 × 16 × 4 = 1,024 bytes
Load tracking: 243 × 8 = 1,944 bytes
Total base: ~27 KB
With 20% overhead: ~32 KB for router state

Per-expert networks (typical 3-layer):
Weights: 3 layers × 64×128 × 2 bits = 6 KB per expert
243 experts × 6 KB = 1,458 KB = ~1.4 MB

Total memory: ~1.5 MB minimum
Recommended: 16-64 MB for buffers, caching, and growth
```

### Scaling Table

| Experts | Specialization Dim | Router Memory | Expert Networks (3-layer) | Total |
|---------|-------------------|---------------|---------------------------|-------|
| 16 | 64 | 2 KB | 96 KB | ~100 KB |
| 64 | 128 | 8 KB | 384 KB | ~400 KB |
| 128 | 128 | 15 KB | 768 KB | ~800 KB |
| 243 | 128 | 27 KB | 1.4 MB | ~1.5 MB |
| 243 | 256 | 54 KB | 2.8 MB | ~3 MB |

## Topology Selection

### Decision Tree

```
Number of Experts?
├── ≤ 32 → Use RING topology
│   └── Simple, fast, good locality
├── 33-128 → Use SMALL_WORLD topology
│   └── Balance of locality and connectivity
└── ≥ 129 → Use HIERARCHICAL topology
    └── Required for 243-expert scale
```

### Ring Topology (≤32 Experts)

```cpp
config.topology = UnifiedMoEConfig::TopologyType::RING;
config.use_hierarchical_selection = false;
```

**Characteristics:**
- O(n) edges (2 per expert)
- Fast message passing
- Good for small clusters
- Simple implementation

### Small-World Topology (33-128 Experts)

```cpp
config.topology = UnifiedMoEConfig::TopologyType::SMALL_WORLD;
config.small_world_rewiring_prob = 0.3;  // 30% rewiring
config.small_world_k = 4;                // 4 neighbors
config.use_hierarchical_selection = true;
```

**Characteristics:**
- O(n log n) average path length
- Sparse: O(n) edges
- Efficient for medium scale
- Watts-Strogatz model

### Hierarchical Topology (≥129 Experts, Required for 243)

```cpp
config.topology = UnifiedMoEConfig::TopologyType::HIERARCHICAL;
config.cluster_size = 16;       // Experts per cluster
config.num_clusters = 16;       // 16 × 16 = 256 capacity
config.use_hierarchical_selection = true;
```

**Characteristics:**
- Two-level routing: cluster → expert
- O(n) total edges (sparse)
- Scales to thousands of experts
- Matches distributed memory hierarchies

**How it works:**
1. Route to cluster (select 2-4 clusters)
2. Route within cluster (select top-K from cluster)
3. Total complexity: O(√n) instead of O(n)

## Active Expert Selection (Top-K)

### Recommended K Values

| Total Experts | Recommended K | Notes |
|--------------|---------------|-------|
| 16 | 4 | 25% activated |
| 64 | 8 | 12.5% activated |
| 128 | 12 | 9.4% activated |
| 243 | 16 | 6.6% activated |

### Configuration

```cpp
// For 243 experts, activate top 16
config.active_experts = 16;

// Enable parallel Top-K selection
config.use_parallel_topk = true;
config.topk_batch_size = 64;
```

### Energy vs Performance Trade-off

```cpp
// Higher K = more compute, potentially better quality
config.active_experts = 32;  // More experts per token

// Lower K = less compute, faster inference
config.active_experts = 8;   // Fewer experts per token
```

## Load Balancing

### Configuration

```cpp
// Enable load balancing
config.enable_load_balancing = true;

// Rebalance every 100 routing operations
config.load_balance_interval = 100;

// Load balancing loss weight
// Higher = more aggressive balancing (but may hurt specialization)
config.load_balance_alpha = 0.01f;  // 1% load balancing penalty
```

### Monitoring Load Balance

```cpp
auto stats = router.GetLoadStats();

// Imbalance score: 0 = perfect, 1 = worst
float score = stats.imbalance_score;

if (score < 0.2) {
    // Excellent: experts well-utilized
} else if (score < 0.5) {
    // Good: some imbalance (healthy specialization)
} else {
    // Poor: rebalance needed
    router.RebalanceLoads();
}
```

### Healthy Imbalance

**Good specialization shows as uneven utilization:**
- Some experts: 5-10% utilization (specialists)
- Other experts: 0.5-1% utilization (rare patterns)
- Imbalance score: 0.2-0.4

**Bad balance (no specialization):**
- All experts: ~4% utilization (uniform)
- Imbalance score: < 0.1

## Energy Budget Configuration

### Energy Levels

```cpp
enum class EnergyTrit {
    LOW,     // < 0.5 pJ per operation
    MEDIUM,  // 0.5-1.0 pJ per operation
    HIGH     // > 1.0 pJ per operation
};
```

### Configuration by Use Case

```cpp
// Edge / Battery-powered device
config.energy_budget = ternary::EnergyTrit::LOW;
config.energy_aware_routing = true;
config.active_experts = 8;  // Fewer experts = less energy

// Data center / Desktop
config.energy_budget = ternary::EnergyTrit::MEDIUM;
config.active_experts = 16;

// High-performance computing
config.energy_budget = ternary::EnergyTrit::HIGH;
config.active_experts = 32;  // More experts for quality
```

### Energy-Aware Routing

```cpp
// Router prefers low-energy paths when energy budget is tight
config.energy_aware_routing = true;

// Energy cost is factored into routing scores
// Low-energy experts preferred when close in specialization
```

## Specialization Dimension

### Choosing Dimension

| Use Case | Recommended Dimension | Rationale |
|----------|---------------------|-----------|
| Simple tasks | 64 | Fast routing, less memory |
| General NLP | 128 | Good balance |
| Code generation | 256 | Complex patterns need more dimensions |
| Multimodal | 512 | Diverse input types |

### Configuration

```cpp
config.specialization_dim = 128;  // Standard for 243-expert
```

### Dimension Scaling

Higher dimension:
- ✓ Better discrimination between experts
- ✓ More nuanced specialization
- ✗ More memory per expert
- ✗ Slower routing computation

## Validation and Testing

### Validate 243-Expert Configuration

```cpp
// Check configuration is valid for 243 experts
bool valid = router.Validate243Config();

// Should verify:
// - Hierarchical selection enabled
// - Appropriate topology
// - Cluster sizes match
```

### Memory Estimation

```cpp
// Estimate memory usage before deployment
size_t bytes = router.EstimateMemoryUsage();
std::cout << "Estimated memory: " << (bytes / 1024 / 1024) << " MB" << std::endl;

// Ensure available memory > 2x estimate for buffers
assert(available_memory > 2 * bytes);
```

### Performance Test

```cpp
// Benchmark routing latency
std::vector<std::vector<ternary::Trit>> test_inputs(1000, input);

auto start = std::chrono::high_resolution_clock::now();
for (const auto& inp : test_inputs) {
    router.Route(inp);
}
auto end = std::chrono::high_resolution_clock::now();

auto avg_latency = std::chrono::duration_cast<std::chrono::microseconds>(
    end - start
  // ... (truncated)
  // See source for complete code
```

## Production Deployment

### Recommended Configuration (Copy-Paste)

```cpp
// === 243-EXPERT PRODUCTION CONFIG ===
auto config = UnifiedMoEConfig::LargeScale();

// Topology: Hierarchical for 243-expert scale
config.topology = UnifiedMoEConfig::TopologyType::HIERARCHICAL;
config.cluster_size = 16;
config.num_clusters = 16;

// Routing: Top-16 of 243
config.active_experts = 16;
config.use_hierarchical_selection = true;
  // ... (truncated)
  // See source for complete code
```

### Docker Deployment

```dockerfile
FROM qminiwasm/moe:latest

# Configure 243-expert model
ENV MOE_TOTAL_EXPERTS=243
ENV MOE_ACTIVE_EXPERTS=16
ENV MOE_TOPOLOGY=hierarchical
ENV MOE_CLUSTER_SIZE=16

# Memory limits
ENV MEMORY_LIMIT=4g

  // ... (truncated)
  // See source for complete code
```

### Kubernetes Configuration

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: moe-243-expert
spec:
  replicas: 3
  template:
    spec:
      containers:
      - name: moe
        image: qminiwasm/moe:v2.0
  // ... (truncated)
  // See source for complete code
```

## Troubleshooting

### High Memory Usage

**Symptoms:** OOM errors, swapping

**Solutions:**
1. Check topology (should be HIERARCHICAL, not DENSE)
2. Reduce specialization_dim (try 64 instead of 128)
3. Use smaller active_experts (try 8 instead of 16)

### Slow Routing (>200μs)

**Symptoms:** High latency, poor throughput

**Solutions:**
1. Enable hierarchical_selection
2. Enable parallel_topk
3. Reduce cluster_size (try 8 instead of 16)
4. Use SYCL acceleration

### No Expert Specialization

**Symptoms:** Uniform utilization, low model quality

**Solutions:**
1. Reduce load_balance_alpha (try 0.001)
2. Increase training data diversity
3. Check that Forward-Forward training is working
4. Verify negative samples are being generated

### Load Imbalance Crashes

**Symptoms:** Router throws exceptions, some experts unused

**Solutions:**
1. Check config.enable_load_balancing is true
2. Verify expert registration succeeded
3. Check that all 243 experts have valid specializations

## Migration from Smaller Scale

### From 64 Experts to 243

```cpp
// 1. Export 64-expert configuration
auto old_config = UnifiedMoEConfig::MediumScale();

// 2. Create 243-expert configuration
auto new_config = UnifiedMoEConfig::LargeScale();

// 3. Copy relevant settings
new_config.specialization_dim = old_config.specialization_dim;
new_config.active_experts = old_config.active_experts;  // Keep same K

// 4. Enable hierarchical routing
  // ... (truncated)
  // See source for complete code
```

### Preserving Expert Knowledge

When scaling up, you can preserve knowledge from smaller deployments:

```cpp
// 1. Load old 64-expert router
auto old_router = LoadRouter("64_expert.chk");

// 2. Create new 243-expert router
auto new_router = Create243ExpertRouter();

// 3. Copy specializations from old experts to new
// (cluster 0 experts in new get old expert specializations)
for (size_t i = 0; i < 64; ++i) {
    auto spec = old_router.GetExpert(i)->GetSpecialization();
    new_router.UpdateSpecialization(i, spec);
  // ... (truncated)
  // See source for complete code
```

## See Also

- [Expert Network Architecture Guide](Expert Networks)
- [MoE Training Guide](MOE Training)
- [API Reference: UnifiedMoEConfig](API-Unified Config.md)
- [Performance Tuning](Guides-Performance Tuning.md)

---

*Version: 1.0*  
*Last Updated: April 2026*
