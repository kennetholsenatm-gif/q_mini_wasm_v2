# Core API Reference

This document provides comprehensive API documentation for the q_mini_wasm_v2 framework.

## Table of Contents

1. [Ternary Operations](#coreternary--trit-operations)
2. [Stabilizer Tableau](#corestabilizer--tableau)
3. [MoE Router](#coremoe--expert-router)
4. [Forward-Forward Learning](#corelearning--forward-forward-algorithm)
5. [Runtime Orchestration](#runtime--orchestration)

---

## `core::ternary` — Trit Operations

### `Trit` Enum

```cpp
enum class Trit : int8_t {
    NEGATIVE = -1,  // |1⟩ quantum state
    ZERO     =  0,  // |0⟩ quantum state
    POSITIVE =  1   // |2⟩ quantum state
};
```

### GF(3) Operations

| Function | Signature | Description |
|---|---|---|
| `to_gf3` | `int8_t(Trit t)` | Convert Trit to GF(3) value {0,1,2} |
| `from_gf3` | `Trit(int8_t val)` | Convert GF(3) value back to Trit |
| `gf3_add` | `Trit(Trit a, Trit b)` | Addition mod 3 |
| `gf3_mul` | `Trit(Trit a, Trit b)` | Multiplication mod 3 |
| `gf3_neg` | `Trit(Trit a)` | Negation mod 3 |

### Binary Coded Ternary (BCT)

```cpp
struct BCT {
    uint8_t high;  // AH
    uint8_t low;   // AL
    
    static constexpr BCT from_trit(Trit t) noexcept;
    constexpr Trit to_trit() const noexcept;
};
```

### TritBlock5 Packing

```cpp
struct TritBlock5 {
    Trit trits[5];
    
    // Pack 5 trits into 8 bits (99.06% entropy efficiency)
    uint8_t pack() const;
    
    // Unpack 8 bits back to 5 trits
    static TritBlock5 unpack(uint8_t byte);
};
```

**Example Usage:**

```cpp
using namespace q_mini_wasm_v2::core::ternary;

// Create trits
Trit a = Trit::POSITIVE;
Trit b = Trit::NEGATIVE;

// GF(3) operations
Trit sum = gf3_add(a, Trit::ZERO);  // 1 + 0 = 1
Trit product = gf3_mul(a, b);       // 1 * (-1) = -1

// Pack/unpack
TritBlock5 block;
block.trits[0] = Trit::POSITIVE;
block.trits[1] = Trit::ZERO;
block.trits[2] = Trit::NEGATIVE;
block.trits[3] = Trit::POSITIVE;
block.trits[4] = Trit::ZERO;

uint8_t packed = block.pack();  // Pack into 8 bits
auto unpacked = TritBlock5::unpack(packed);  // Unpack
```

---

## `core::stabilizer` — Tableau

### StabilizerTableau Class

```cpp
class StabilizerTableau {
public:
    // Construction
    explicit StabilizerTableau(size_t num_qutrits);
    StabilizerTableau(const StabilizerTableau& other);  // Copy
    StabilizerTableau(StabilizerTableau&& other) noexcept;  // Move
    StabilizerTableau& operator=(const StabilizerTableau& other);
    ~StabilizerTableau();
    
    // Clifford Gate Operations
    void apply_hadamard(size_t target_qutrit);
    void apply_phase(size_t target_qutrit);
    void apply_csum(size_t control, size_t target);
    
    // Measurement
    Trit measure(size_t target_qutrit);
    
    // State Queries
    size_t num_qutrits() const;
    bool is_stabilizer(size_t row) const;
};
```

### Factory Function

```cpp
std::unique_ptr<StabilizerTableau> create_tableau(size_t n_qutrits);
```

### Method Details

| Method | Signature | Complexity | Description |
|---|---|---|---|
| `apply_hadamard` | `void(size_t q)` | O(n) | Apply Hadamard gate to qutrit q (X↔Z swap) |
| `apply_phase` | `void(size_t q)` | O(n) | Apply Phase gate to qutrit q (Z→ZX) |
| `apply_csum` | `void(size_t a, size_t b)` | O(n) | Apply Controlled-SUM between qutrits a and b |
| `measure` | `Trit(size_t q)` | O(n²) | Measure qutrit q and collapse state |

### Tableau Structure

```
tableau[2n][2n] : GF(3) matrix
  rows 0..n-1   : X generators (destabilizers)
  rows n..2n-1  : Z generators (stabilizers)
phase[2n]       : Phase vector
```

**Example Usage:**

```cpp
using namespace q_mini_wasm_v2::core::stabilizer;

// Create a 4-qutrit tableau
auto tableau = create_tableau(4);

// Apply Clifford gates
tableau->apply_hadamard(0);  // Put qutrit 0 in superposition
tableau->apply_phase(1);     // Apply phase to qutrit 1
tableau->apply_csum(0, 1);   // Entangle qutrits 0 and 1

// Measure qutrit
Trit result = tableau->measure(0);
```

---

## `core::moe` — Expert Router

### ExpertConfig Struct

```cpp
struct ExpertConfig {
    size_t total_experts;      // Total number of available experts
    size_t active_experts;     // Number of experts to activate (Top-K)
    size_t routing_qutrits;    // Number of qutrits for routing decisions
};
```

### MoERouter Class

```cpp
class MoERouter {
public:
    // Construction
    explicit MoERouter(const ExpertConfig& config);
    ~MoERouter();
    
    // Routing Operations
    std::vector<size_t> route_topk(const std::vector<ternary::Trit>& input);
    std::vector<double> compute_routing_logits(const std::vector<ternary::Trit>& input);
    std::vector<double> entangled_route(
        stabilizer::StabilizerTableau& tableau,
        const std::vector<ternary::Trit>& input
    );
    
    // Tropical Geometry Operations
    static double tropical_inner_product(
        const std::vector<double>& a,
        const std::vector<double>& b
    );
    static double tropical_add(double a, double b);
    static double tropical_mul(double a, double b);
};
```

### Factory Function

```cpp
std::unique_ptr<MoERouter> create_moe_router(const ExpertConfig& config);
```

### Method Details

| Method | Signature | Description |
|---|---|---|
| `route_topk` | `vector<size_t>(vector<Trit>)` | Select Top-K experts via tropical geometry |
| `compute_routing_logits` | `vector<double>(vector<Trit>)` | Compute routing logits using tropical inner product |
| `entangled_route` | `vector<double>(Tableau&, vector<Trit>)` | Entanglement-based routing via stabilizer tableau |
| `tropical_inner_product` | `double(vector<double>, vector<double>)` | Tropical inner product: max_i(a_i + b_i) |
| `tropical_add` | `double(double, double)` | Tropical addition: max(a, b) |
| `tropical_mul` | `double(double, double)` | Tropical multiplication: a + b |

**Example Usage:**

```cpp
using namespace q_mini_wasm_v2::core::moe;
using namespace q_mini_wasm_v2::core::ternary;

// Configure router: 8 experts, Top-2 selection, 4 routing qutrits
ExpertConfig config{8, 2, 4};
auto router = create_moe_router(config);

// Route input through experts
std::vector<Trit> input = {
    Trit::POSITIVE,
    Trit::ZERO,
    Trit::NEGATIVE,
    Trit::POSITIVE
};

auto selected_experts = router->route_topk(input);
// Returns indices of 2 selected experts

// Compute routing logits
auto logits = router->compute_routing_logits(input);

// Tropical geometry operations
double a = 5.0, b = 3.0;
double max_val = MoERouter::tropical_add(a, b);  // max(5,3) = 5
double sum_val = MoERouter::tropical_mul(a, b);  // 5+3 = 8
```

---

## `core::learning` — Forward-Forward Algorithm

### FFConfig Struct

```cpp
struct FFConfig {
    size_t num_layers;              // Number of hidden layers
    size_t neurons_per_layer;       // Neurons in each layer
    double learning_rate;           // Local learning rate
    double positive_threshold;      // Goodness threshold for positive data
    double negative_threshold;      // Goodness threshold for negative data
};
```

### LayerGoodness Struct

```cpp
struct LayerGoodness {
    double positive_goodness;   // Goodness for positive (real) data
    double negative_goodness;   // Goodness for negative (corrupted) data
    double delta;               // Difference: positive - negative
};
```

### ForwardForwardLearner Class

```cpp
class ForwardForwardLearner {
public:
    // Construction
    explicit ForwardForwardLearner(const FFConfig& config);
    ~ForwardForwardLearner();
    
    // Training Operations
    LayerGoodness train_layer(
        size_t layer_idx,
        const std::vector<std::vector<ternary::Trit>>& positive_data,
        const std::vector<std::vector<ternary::Trit>>& negative_data
    );
    
    std::vector<std::vector<ternary::Trit>> generate_negative_samples(
        const std::vector<std::vector<ternary::Trit>>& positive_data
    );
    
    double compute_goodness(const std::vector<ternary::Trit>& activations) const;
    double compute_entangled_goodness(const stabilizer::StabilizerTableau& tableau) const;
};
```

### Method Details

| Method | Signature | Description |
|---|---|---|
| `train_layer` | `LayerGoodness(size_t, vector<vector<Trit>>, vector<vector<Trit>>)` | Train one layer using Forward-Forward algorithm |
| `generate_negative_samples` | `vector<vector<Trit>>(vector<vector<Trit>>)` | Generate negative samples via corruption |
| `compute_goodness` | `double(vector<Trit>)` | Compute goodness using tropical inner product |
| `compute_entangled_goodness` | `double(Tableau&)` | Compute goodness using Entanglement Entropy |

**Example Usage:**

```cpp
using namespace q_mini_wasm_v2::core::learning;
using namespace q_mini_wasm_v2::core::ternary;

// Configure learner
FFConfig config{
    .num_layers = 3,
    .neurons_per_layer = 128,
    .learning_rate = 0.01,
    .positive_threshold = 0.5,
    .negative_threshold = -0.5
};

auto learner = std::make_unique<ForwardForwardLearner>(config);

// Prepare training data
std::vector<std::vector<Trit>> positive_samples = {
    {Trit::POSITIVE, Trit::ZERO, Trit::NEGATIVE},
    {Trit::ZERO, Trit::POSITIVE, Trit::POSITIVE}
};

// Generate negative samples (corrupted versions)
auto negative_samples = learner->generate_negative_samples(positive_samples);

// Train layer 0
LayerGoodness result = learner->train_layer(0, positive_samples, negative_samples);
std::cout << "Layer 0 delta: " << result.delta << std::endl;
```

---

## `runtime` — Orchestration

### TaskType Enum

```cpp
enum class TaskType {
    TABLEAU_UPDATE,
    MOE_ROUTING,
    FORWARD_FORWARD,
    MEASUREMENT
};
```

### RuntimeConfig Struct

```cpp
struct RuntimeConfig {
    size_t num_worker_threads;      // Number of worker threads
    size_t max_queue_size;          // Maximum pending tasks
    bool enable_async;              // Enable async execution
    bool enable_flash_cim;          // Enable Flash-CIM interface
};
```

### RuntimeOrchestrator Class

```cpp
class RuntimeOrchestrator {
public:
    // Construction
    explicit RuntimeOrchestrator(const RuntimeConfig& config);
    ~RuntimeOrchestrator();
    
    // Task Submission
    std::future<void> submit_tableau_update(
        core::stabilizer::StabilizerTableau& tableau,
        std::function<void(core::stabilizer::StabilizerTableau&)> gate_func
    );
    
    std::future<std::vector<size_t>> submit_moe_routing(
        core::moe::MoERouter& router,
        const std::vector<core::ternary::Trit>& input
    );
    
    std::future<double> submit_forward_forward(
        core::learning::ForwardForwardLearner& learner,
        size_t layer_idx,
        const std::vector<std::vector<core::ternary::Trit>>& positive_data,
        const std::vector<std::vector<core::ternary::Trit>>& negative_data
    );
    
    // Synchronization
    void wait_all();
    
    // Status
    size_t pending_tasks() const;
    size_t completed_tasks() const;
};
```

### Factory Function

```cpp
std::unique_ptr<RuntimeOrchestrator> create_orchestrator(const RuntimeConfig& config);
```

### Method Details

| Method | Signature | Description |
|---|---|---|
| `submit_tableau_update` | `future<void>(Tableau&, function<void(Tableau&)>)` | Submit async tableau update task |
| `submit_moe_routing` | `future<vector<size_t>>(MoERouter&, vector<Trit>)` | Submit async MoE routing task |
| `submit_forward_forward` | `future<double>(FFLearner&, size_t, vector<vector<Trit>>, vector<vector<Trit>>)` | Submit async Forward-Forward training task |
| `wait_all` | `void()` | Block until all tasks complete |
| `pending_tasks` | `size_t()` | Get number of pending tasks |
| `completed_tasks` | `size_t()` | Get number of completed tasks |

**Example Usage:**

```cpp
using namespace q_mini_wasm_v2::runtime;
using namespace q_mini_wasm_v2::core::stabilizer;
using namespace q_mini_wasm_v2::core::moe;
using namespace q_mini_wasm_v2::core::learning;

// Configure runtime
RuntimeConfig config{
    .num_worker_threads = 4,
    .max_queue_size = 100,
    .enable_async = true,
    .enable_flash_cim = false
};

auto orchestrator = create_orchestrator(config);

// Create components
auto tableau = create_tableau(8);
ExpertConfig expert_config{8, 2, 4};
auto router = create_moe_router(expert_config);

// Submit async tasks
auto future1 = orchestrator->submit_tableau_update(
    *tableau,
    [](StabilizerTableau& t) {
        t.apply_hadamard(0);
        t.apply_csum(0, 1);
    }
);

std::vector<core::ternary::Trit> input = {
    core::ternary::Trit::POSITIVE,
    core::ternary::Trit::ZERO,
    core::ternary::Trit::NEGATIVE
};

auto future2 = orchestrator->submit_moe_routing(*router, input);

// Wait for all tasks to complete
orchestrator->wait_all();

// Get results
auto experts = future2.get();
```

---

## Complete Example

```cpp
#include "q_mini_wasm_v2/core/ternary/trit.hpp"
#include "q_mini_wasm_v2/core/stabilizer/tableau.hpp"
#include "q_mini_wasm_v2/core/moe/router.hpp"
#include "q_mini_wasm_v2/core/learning/forward_forward.hpp"
#include "q_mini_wasm_v2/runtime/orchestrator.hpp"

using namespace q_mini_wasm_v2;

int main() {
    // 1. Create stabilizer tableau
    auto tableau = core::stabilizer::create_tableau(4);
    tableau->apply_hadamard(0);
    tableau->apply_csum(0, 1);
    
    // 2. Configure MoE router
    core::moe::ExpertConfig expert_config{8, 2, 4};
    auto router = core::moe::create_moe_router(expert_config);
    
    // 3. Prepare input
    std::vector<core::ternary::Trit> input = {
        core::ternary::Trit::POSITIVE,
        core::ternary::Trit::ZERO,
        core::ternary::Trit::NEGATIVE,
        core::ternary::Trit::POSITIVE
    };
    
    // 4. Route input through experts
    auto experts = router->route_topk(input);
    
    // 5. Configure Forward-Forward learner
    core::learning::FFConfig ff_config{
        .num_layers = 2,
        .neurons_per_layer = 64,
        .learning_rate = 0.01,
        .positive_threshold = 0.5,
        .negative_threshold = -0.5
    };
    auto learner = std::make_unique<core::learning::ForwardForwardLearner>(ff_config);
    
    // 6. Configure runtime
    runtime::RuntimeConfig runtime_config{
        .num_worker_threads = 4,
        .max_queue_size = 100,
        .enable_async = true,
        .enable_flash_cim = false
    };
    auto orchestrator = runtime::create_orchestrator(runtime_config);
    
    return 0;
}
```

---

## See Also

- [Architecture Overview](../architecture/overview.md) - System design
- [Build Guide](../guides/building.md) - Build instructions
- [Contributing Guide](../guides/contributing.md) - Development workflow
