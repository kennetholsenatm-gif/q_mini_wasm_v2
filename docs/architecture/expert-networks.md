# Expert Network Architecture Guide

## Overview

This guide describes how to design and implement expert networks for the 243-expert MoE (Mixture of Experts) system in q_mini_wasm_v2.

## Expert Network Basics

### What is an Expert?

An **expert** is a specialized neural network that handles specific types of inputs. In the MoE framework:
- Each of the 243 experts specializes in different input patterns
- The router directs inputs to the most appropriate experts (typically 16 out of 243)
- Experts train independently using Forward-Forward learning

### Expert Network Interface

All expert networks must inherit from `ExpertNetwork`:

```cpp
class MyExpert : public ExpertNetwork {
public:
    std::vector<ternary::Trit> Forward(
        const std::vector<ternary::Trit>& input
    ) override;
    
    int32_t TrainForwardForward(
        const std::vector<ternary::Trit>& positive,
        const std::vector<ternary::Trit>& negative
    ) override;
};
```

## GF(3) Ternary Arithmetic

### Why GF(3)?

The framework uses GF(3) (Galois Field of order 3) for:
- **Energy efficiency**: Ternary operations use ~60% less energy than floating-point
- **Hardware alignment**: Matches ternary memory and flash-CiM architectures
- **Theoretical foundation**: Connects to quantum stabilizer codes

### Ternary Values

Values are in the set **{-1, 0, 1}** represented as:
```cpp
enum class Trit {
    NEGATIVE = -1,  // Logical true/false or -1
    ZERO = 0,       // Uncertainty/unknown
    POSITIVE = 1    // Logical true/false or +1
};
```

### Arithmetic Operations

**Standard GF(3):**
```
Addition (mod 3):  1 + 1 = -1,  1 + (-1) = 0,  -1 + (-1) = 1
Multiplication:    1 * 1 = 1,   1 * (-1) = -1, (-1) * (-1) = 1
```

**Tropical (Max-Plus) Algebra:**
```
Addition:  a ⊕ b = max(a, b)
Multiply:  a ⊗ b = a + b
```

## GF3LinearLayer

### Basic Usage

```cpp
#include "core/moe/gf3_layers.hpp"

// Create layer: 64 inputs -> 128 outputs
GF3LinearLayer::LayerConfig config;
config.input_dim = 64;
config.output_dim = 128;
config.use_bias = true;
config.use_tropical = false;  // Use standard GF(3)

GF3LinearLayer layer(config);

// Initialize with random ternary weights
layer.InitializeWeights(42);  // seed=42

// Forward pass
std::vector<ternary::Trit> input(64, ternary::Trit::POSITIVE);
auto output = layer.Forward(input);  // 128 outputs
```

### Tropical vs Standard

**Use Standard GF(3) for:**
- Standard neural network behavior
- Maximum representational capacity
- Compatibility with standard architectures

**Use Tropical for:**
- Maximum energy efficiency
- Shortest-path / Viterbi-like computations
- When "best single feature" is more important than "sum of features"

### Weight Initialization

Ternary weights are initialized randomly from {-1, 0, 1}:
```cpp
layer.InitializeWeights(seed);
```

Sparsity (fraction of zeros) is typically ~33% with uniform initialization.

## Multi-Layer Experts

### Building an Expert

```cpp
// Configuration
ExpertNetwork::ExpertConfig config;
config.input_dim = 64;
config.output_dim = 64;
config.hidden_dim = 128;
config.num_layers = 3;
config.use_activation = true;

// Create expert
GF3MultiLayerExpert expert(config);

// Initialize all layers
expert.InitializeAllLayers(42);

// Forward pass
auto output = expert.Forward(input);
```

### Architecture Patterns

**Standard MLP Expert:**
```
Input (dim) -> Hidden (2x dim) -> Hidden (2x dim) -> Output (dim)
```

**Bottleneck Expert (efficient):**
```
Input (dim) -> Hidden (0.5x dim) -> Hidden (0.5x dim) -> Output (dim)
```

**Wide Expert (high capacity):**
```
Input (dim) -> Hidden (4x dim) -> Output (dim)
```

## Forward-Forward Training

### No Backpropagation

Unlike standard neural networks, experts use **Forward-Forward** learning:
- Each layer learns independently
- No gradient chain through network
- Local goodness metrics drive learning

### The Forward-Forward Algorithm

1. **Generate negative sample**: Corrupt the positive (real) sample
2. **Compute goodness for both**: Goodness = sum of |activations|
3. **Update weights**: Reinforce if positive_goodness > negative_goodness

```cpp
// Training iteration
int32_t delta = expert.TrainForwardForward(positive_sample, negative_sample);

// delta > 0: Expert learned something
// delta < 0: Expert got confused (will self-correct)
// delta = 0: No change needed
```

### Hebbian Weight Update

Weights update via:
```
Δw = learning_rate × (goodness_pos - goodness_neg) × input × output
```

In GF(3), this becomes ternary multiplication and addition.

## Expert Specialization

### How Experts Specialize

1. **Router directs similar inputs to same experts** (via specialization vectors)
2. **Experts see mostly similar inputs** (clustering effect)
3. **Forward-Forward adapts weights to common patterns**
4. **Result: Expert becomes specialist for that input type**

### Specialization Vectors

Each expert has a **specialization vector** used by the router:
```cpp
// Router computes: score = tropical_inner_product(input, expert.specialization)
// High score = expert is good for this input
```

Specializations are initialized randomly and evolve during training.

### Monitoring Specialization

Track expert utilization:
```cpp
auto stats = router.GetLoadStats();
// stats.utilization_rates shows which experts are used most
```

Healthy specialization: **utilization_rates are uneven** (some experts popular, others not)
- Perfect balance (uniform rates) = no specialization
- High variance = good specialization

## Design Patterns

### Pattern 1: Task-Based Experts

Experts specialize by task type:
- Experts 0-15: Code generation
- Experts 16-31: Natural language
- Experts 32-47: Mathematical reasoning
- etc.

**Implementation:** Initialize specializations to cluster by task.

### Pattern 2: Token-Based Experts

Experts specialize by token/character patterns:
- Expert 0: Handles inputs starting with "def"
- Expert 1: Handles numeric inputs
- etc.

**Implementation:** Pre-train specializations on token distributions.

### Pattern 3: Hierarchical Experts

Clusters of experts form higher-level specializations:
- Cluster 0: Programming languages
  - Expert 0: Python
  - Expert 1: C++
  - Expert 2: JavaScript
- Cluster 1: Natural languages
  - Expert 16: English
  - Expert 17: Spanish

**Implementation:** Use `TopologyType::HIERARCHICAL` in config.

## Performance Considerations

### Memory per Expert

```
Weights: input_dim × output_dim × 2 bits (ternary packing)
Bias: output_dim × 2 bits
Activations: batch_size × output_dim × 2 bits

Example (64->128 layer):
Weights: 64 × 128 × 2 = 16,384 bits = 2 KB
Bias: 128 × 2 = 256 bits = 32 bytes
Total: ~2 KB per layer

3-layer expert with 64-dim: ~6 KB
243 experts × 6 KB = ~1.5 MB (excluding overhead)
```

### Computation per Forward Pass

```
Standard GF(3): input_dim × output_dim multiply-adds
Tropical: input_dim × output_dim max-adds

243 experts × 16 selected × (64 × 128) ops = ~33M ops per batch
At 0.35 pJ/op: ~11.5 μJ per batch
```

### Parallelization

Use SYCL for parallel expert execution:
```cpp
// Submit experts to GPU in parallel
for (auto& expert : selected_experts) {
    queue.submit([&](handler& h) {
        h.parallel_for(..., [=] {
            expert->Forward(input);
        });
    });
}
queue.wait();
```

## Best Practices

### 1. Start Small

Test with 16 experts before scaling to 243:
```cpp
auto config = UnifiedMoEConfig::SmallScale();  // 16 experts
```

### 2. Monitor Load Balance

Ensure experts specialize:
```cpp
if (metrics.load_balance_score < 0.5) {
    // Good: experts are specializing
} else {
    // Bad: all experts doing similar work
    // Adjust load_balance_alpha or topology
}
```

### 3. Use Hierarchical Routing at Scale

For 243 experts, hierarchical routing is essential:
```cpp
config.use_hierarchical_selection = true;
config.topology = UnifiedMoEConfig::TopologyType::HIERARCHICAL;
```

### 4. Checkpoint Regularly

Save training progress:
```cpp
trainer.SaveCheckpoint("moe_epoch_" + std::to_string(epoch) + ".chk");
```

### 5. Validate GF(3) Compliance

Ensure no floating-point pollution:
```python
# Run validation
python scripts/validate_gf3.py --check-experts
```

## Troubleshooting

### Problem: All experts have similar utilization

**Cause**: No specialization occurring

**Solutions**:
- Increase `load_balance_alpha` to encourage diversity
- Use more diverse training data
- Check that specialization vectors are being updated

### Problem: Training not converging (delta ≈ 0)

**Cause**: Experts not learning, possibly saturated

**Solutions**:
- Increase learning rate (try ±2 instead of ±1)
- Reduce number of layers
- Check negative sample generation

### Problem: Out of memory at 243 experts

**Cause**: Dense entanglement or too many parameters

**Solutions**:
- Use `TopologyType::SMALL_WORLD` instead of `DENSE`
- Reduce `hidden_dim` in expert config
- Enable memory-mapped expert weights

### Problem: Slow routing with 243 experts

**Cause**: O(n) routing complexity

**Solutions**:
- Enable `use_hierarchical_selection`
- Reduce `active_experts` (try 8 instead of 16)
- Use SYCL-accelerated Top-K selection

## Example: Complete Expert Setup

```cpp
#include "core/moe/unified_config.hpp"
#include "core/moe/unified_router.hpp"
#include "core/moe/moe_trainer.hpp"
#include "core/moe/gf3_layers.hpp"

// 1. Create 243-expert configuration
auto config = UnifiedMoEConfig::LargeScale();
config.active_experts = 16;
config.topology = UnifiedMoEConfig::TopologyType::HIERARCHICAL;
config.enable_load_balancing = true;

// 2. Create router
auto router = Create243ExpertRouter();

// 3. Setup trainer
MoETrainingConfig train_config;
train_config.learning_rate = 1;
train_config.load_balance_alpha = 0.01f;
train_config.training_batch_size = 64;

auto trainer = CreateMoETrainer(*router, train_config);

// 4. Initialize experts
trainer->InitializeExperts(42);

// 5. Load training data
auto data = LoadTrainingData("dataset.bin");

// 6. Train
auto final_metrics = trainer->Train(data, 100);

// 7. Use trained model
auto input = PrepareInput("Hello, world!");
auto result = router->Route(input);
// result.selected_experts contains the chosen experts
```

## See Also

- [243-Expert Configuration Guide](243-expert-config.md)
- [MoE Training Guide](moe-training.md)
- [API Reference: ExpertNetwork](../api/expert_network.md)
- [Architecture: Tropical Geometry](../architecture/tropical-geometry.md)

---

*Version: 1.0*  
*Last Updated: April 2026*
