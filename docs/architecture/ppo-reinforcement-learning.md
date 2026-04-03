# PPO Reinforcement Learning for Stabilizer Circuit Discovery

## Overview

The Proximal Policy Optimization (PPO) system enables continuous stabilizer circuit discovery and self-optimization at the edge. This implementation integrates with the existing Forward-Forward learning system to provide adaptive, on-device training capabilities.

## Key Components

### 1. PPOAgent

The core PPO agent that learns to optimize stabilizer circuits through policy gradient methods.

**Features:**
- Discrete action space for Clifford gate selection
- Experience replay buffer for stable training
- Generalized Advantage Estimation (GAE)
- Clipped surrogate objective for stable policy updates
- Edge-optimized with mixed precision support

**Configuration:**
```cpp
PPOConfig config{
    .learning_rate = 0.0003,
    .gamma = 0.99,
    .lambda = 0.95,
    .epsilon = 0.2,
    .num_qutrits = 8,
    .max_circuit_depth = 16
};
```

### 2. StabilizerEnvironment

The RL environment for stabilizer circuit discovery. The agent learns to apply Clifford gates to transform a stabilizer tableau from initial state to target state.

**Reward Function:**
- **Jaccard Distance**: Measures similarity between current and target states
- **Gate Count Penalty**: Encourages efficient circuit discovery
- **Terminal Reward**: Bonus for reaching target state

**Available Actions:**
| Index | Gate | Description |
|-------|------|-------------|
| 0 | Hadamard | Creates superposition |
| 1 | Phase | Adds phase shift |
| 2 | CSUM | Controlled-SUM entangling gate |
| 3 | CZ | Controlled-Z gate |
| 4 | Pauli X | Bit flip |
| 5 | Pauli Y | Combined flip |
| 6 | Pauli Z | Phase flip |
| 7 | Identity | No operation |

### 3. ContinuousLearner

The main training loop that integrates PPO with Forward-Forward learning for continuous edge optimization.

**Features:**
- Automatic learning rate adaptation
- Integration with Forward-Forward goodness metrics
- Circuit serialization for deployment
- Performance tracking and analytics

## Usage

### Basic Usage

```cpp
#include "core/learning/ppo_agent.hpp"

using namespace q_mini_wasm_v2::core::learning;

// Configure PPO
PPOConfig ppo_config;
ppo_config.num_qutrits = 8;
ppo_config.horizon = 64;

// Configure Forward-Forward
FFConfig ff_config{2, 64, 0.01, 1.0, -1.0};

// Create continuous learner
auto learner = create_continuous_learner(ppo_config, ff_config);

// Define target state (maximally entangled)
std::vector<int8_t> target(ppo_config.num_qutrits * ppo_config.num_qutrits * 4, 1);

// Train for 100 episodes
auto stats = learner->train(100, target);

// Save learned circuit
learner->save_circuit("optimized_circuit.bin");
```

### Integration with Forward-Forward

The PPO system integrates with Forward-Forward learning through the `integrate_learning_signals` method:

```cpp
// Combined learning signal
double combined_reward = 0.7 * ppo_reward + 0.3 * ff_goodness;
```

This allows the system to:
1. Use PPO for long-term circuit structure optimization
2. Use Forward-Forward for local, layer-wise learning
3. Combine both signals for comprehensive optimization

## Edge Optimization Features

### 1. Mixed Precision Support
- Optional mixed precision for reduced memory usage
- Configurable precision for different hardware

### 2. Adaptive Learning Rate
- Automatically reduces learning rate on poor performance
- Prevents divergence during edge deployment

### 3. Early Stopping
- KL divergence monitoring prevents policy collapse
- Configurable threshold for early termination

### 4. Memory Efficiency
- Circular replay buffer
- Configurable buffer size for memory constraints

## Training Statistics

The system tracks comprehensive training statistics:

- **Policy Loss**: Measures policy update magnitude
- **Value Loss**: Measures value function accuracy
- **Entropy**: Measures exploration level
- **Mean Reward**: Average episode reward
- **Episode Length**: Average steps per episode
- **KL Divergence**: Policy stability metric

## Research Alignment

This implementation aligns with research on:
- "Clifford Entanglement AI Protocols Development"
- "Enhancing the QMINIWASM Framework"
- "Forward-Forward Learning without Backpropagation"

The system operates within the parameter-free Clifford space, enabling efficient classical simulation of quantum-inspired circuit optimization.

## Performance Considerations

### Edge Deployment
- Minimal memory footprint (< 1MB for typical configurations)
- Low computational overhead
- Suitable for embedded systems

### Training Speed
- Configurable batch sizes
- Parallel episode collection possible
- Early stopping reduces unnecessary computation

### Model Size
- Policy weights: O(num_actions × state_size)
- Value weights: O(state_size)
- Typical: < 100KB for 8-qutrit systems