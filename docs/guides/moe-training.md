# MoE Training Guide

## Overview

This guide covers end-to-end training of MoE models using Forward-Forward learning. Supports both epoch-based and continuous (autonomous) training modes.

**Supported Scales:** 16 → 64 → 243 → 8192 experts  
**Training Modes:** Epoch-based (default) or Continuous (`--continuous`)

## Training Modes

### Mode A: Epoch-Based (Default)

Fixed-duration training with pre-loaded dataset.

```bash
q_mini_wasm_v2_trainer.exe --epochs 100 --moe-experts 243
```

### Mode B: Continuous/Autonomous (Recommended for Production)

Indefinite training with continuous data acquisition from knowledge engines.

```bash
q_mini_wasm_v2_trainer.exe --continuous --moe-experts 243 --enable-web-apis true
```

**Features of Continuous Mode:**
- Background training thread
- Pause/resume capability
- Live Betti-guided topology optimization
- Continuous API data acquisition

See [Autonomous Training Pipeline](autonomous-pipeline.md) for full API details.

## Quick Start

### Epoch-Based Training

```cpp
#include "core/moe/moe_trainer.hpp"
#include "core/moe/unified_router.hpp"

// Create router (supports up to 8192 experts)
auto router = Create243ExpertRouter();  // 243-expert pre-configured

// Setup trainer
MoETrainingConfig config;
config.learning_rate = 1;
config.training_batch_size = 64;

auto trainer = CreateMoETrainer(*router, config);

// Initialize experts
trainer->InitializeExperts(42);

// Load data
auto data = LoadData("training_data.bin");

// Train for 100 epochs
auto metrics = trainer->Train(data, 100);

// Save
router->SaveCheckpoint("trained_moe.chk");
```

### Continuous Training with Autonomous Pipeline

```cpp
#include "core/training/autonomous_training_pipeline.hpp"

// Configure for continuous training
PipelineConfig config;
config.moe_num_experts = 243;           // Current: 243, Target: 8192
config.enable_knowledge_engine = true;  // Use DataSynthesizer APIs
config.enable_betti_guidance = true;    // Auto-optimize topology

AutonomousTrainingPipeline pipeline;
pipeline.initialize(config);

// Start background training
pipeline.start_training();

// Monitor progress
while (pipeline.get_state() == PipelineState::TRAINING) {
    auto metrics = pipeline.get_metrics();
    // Check goodness delta, Betti numbers, etc.
    std::this_thread::sleep_for(std::chrono::seconds(5));
}

// Save and stop
pipeline.export_model("trained_model.bin");
pipeline.stop_training();
```

See [Data Synthesizer](data-synthesizer.md) for the 15 implemented API integrations.

## Data Preparation

### Format

Training data consists of **positive samples** (real data). Negative samples are generated automatically by corruption.

```cpp
std::vector<std::vector<ternary::Trit>> training_data;

// Each sample is a vector of ternary values
std::vector<ternary::Trit> sample;
sample.push_back(ternary::Trit::POSITIVE);
sample.push_back(ternary::Trit::ZERO);
sample.push_back(ternary::Trit::NEGATIVE);
// ... more values

training_data.push_back(sample);
```

### Preparing Text Data

```cpp
// Convert text to ternary encoding
std::vector<ternary::Trit> TextToTernary(const std::string& text) {
    std::vector<ternary::Trit> result;
    
    for (char c : text) {
        // Map characters to ternary values
        // Example: A-M -> POSITIVE, N-Z -> NEGATIVE, space/punct -> ZERO
        if (c >= 'A' && c <= 'M') {
            result.push_back(ternary::Trit::POSITIVE);
        } else if (c >= 'N' && c <= 'Z') {
            result.push_back(ternary::Trit::NEGATIVE);
        } else {
            result.push_back(ternary::Trit::ZERO);
        }
    }
    
    return result;
}
```

### Data Loading

```cpp
std::vector<std::vector<ternary::Trit>> LoadData(const std::string& path) {
    std::vector<std::vector<ternary::Trit>> data;
    
    std::ifstream file(path, std::ios::binary);
    
    // Read number of samples
    size_t num_samples;
    file.read(reinterpret_cast<char*>(&num_samples), sizeof(num_samples));
    
    for (size_t i = 0; i < num_samples; ++i) {
        // Read sample size
        size_t sample_size;
        file.read(reinterpret_cast<char*>(&sample_size), sizeof(sample_size));
        
        // Read sample data
        std::vector<ternary::Trit> sample(sample_size);
        file.read(reinterpret_cast<char*>(sample.data()), 
                  sample_size * sizeof(ternary::Trit));
        
        data.push_back(sample);
    }
    
    return data;
}
```

### Data Augmentation

For better training, augment your data:

```cpp
// Add noise to samples
std::vector<ternary::Trit> AddNoise(
    const std::vector<ternary::Trit>& sample,
    float noise_prob = 0.1
) {
    static std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0, 1.0);
    std::uniform_int_distribution<int> val_dist(-1, 1);
    
    auto result = sample;
    for (auto& val : result) {
        if (dist(rng) < noise_prob) {
            val = static_cast<ternary::Trit>(val_dist(rng));
        }
    }
    
    return result;
}
```

## Training Configuration

### Basic Configuration

```cpp
MoETrainingConfig config;

// Forward-Forward settings
config.learning_rate = 1;              // GF(3): typically ±1
config.negative_samples_per_positive = 1;
config.training_batch_size = 64;

// Load balancing
config.load_balance_alpha = 0.01f;   // 1% load balancing penalty
config.rebalance_interval = 100;     // Rebalance every 100 batches

// Capacity
config.tokens_per_expert = 128;
config.capacity_factor = 1.25f;

// Convergence
config.min_goodness_delta_threshold = 0.1f;
config.max_epochs = 100;
config.early_stopping_patience = 10;

// Monitoring
config.verbose = true;
config.log_interval = 10;
```

### Configuration for Different Scenarios

#### Fast Prototyping (Small Dataset)

```cpp
config.training_batch_size = 16;     // Small batches
config.max_epochs = 20;              // Fewer epochs
config.log_interval = 1;             // Frequent logging
config.verbose = true;
```

#### Production Training (Large Dataset)

```cpp
config.training_batch_size = 256;    // Large batches
config.max_epochs = 500;             // Many epochs
config.log_interval = 100;           // Infrequent logging
config.early_stopping_patience = 50; // More patience
```

#### Energy-Constrained Training

```cpp
config.training_batch_size = 32;     // Smaller batches
config.negative_samples_per_positive = 1;  // Minimal negatives
config.load_balance_alpha = 0.001f;  // Minimal rebalancing
```

## The Training Loop

### Epoch-Based Training

```cpp
for (size_t epoch = 0; epoch < num_epochs; ++epoch) {
    // 1. Shuffle data
    Shuffle(data);
    
    // 2. Process batches
    for (size_t batch = 0; batch < num_batches; ++batch) {
        auto batch_data = GetBatch(data, batch);
        
        // 3. Train batch
        auto metrics = trainer.TrainBatch(batch_data);
        
        // 4. Log progress
        if (batch % config.log_interval == 0) {
            Log(metrics);
        }
    }
    
    // 5. Validate
    auto val_metrics = Validate(validation_data);
    
    // 6. Check convergence
    if (HasConverged(val_metrics)) {
        break;
    }
    
    // 7. Periodic rebalance
    if (epoch % config.rebalance_interval == 0) {
        trainer.RebalanceLoads();
    }
}
```

### Monitoring Training

```cpp
auto metrics = trainer.GetMetrics();

std::cout << "Epoch: " << metrics.epoch << std::endl;
std::cout << "Goodness Delta: " << metrics.avg_goodness_delta << std::endl;
std::cout << "Load Balance: " << metrics.load_balance_score << std::endl;
std::cout << "Routing Latency: " << metrics.avg_routing_latency_ms << " ms" << std::endl;
```

### Interpreting Metrics

| Metric | Healthy Range | Concerning | Action |
|--------|---------------|------------|--------|
| Goodness Delta | > 0.5 | < 0.1 | Increase learning rate, check data |
| Load Balance Score | 0.2-0.4 | > 0.8 | Reduce load_balance_alpha |
| Routing Latency | < 100μs | > 500μs | Enable hierarchical selection |
| Expert Utilization | Uneven | Uniform | Training working! |

## Forward-Forward Algorithm Details

### Step-by-Step

1. **Positive Sample**: Real data from dataset
   ```cpp
   auto positive = training_data[i];
   ```

2. **Generate Negative**: Corrupt ~10% of values
   ```cpp
   auto negative = trainer.GenerateNegativeSample(positive);
   ```

3. **Route to Experts**: Select top-K experts
   ```cpp
   auto routing = router.Route(positive);
   ```

4. **Compute Goodness**: For both positive and negative
   ```cpp
   int32_t pos_goodness = expert.ComputeGoodness(expert.Forward(positive));
   int32_t neg_goodness = expert.ComputeGoodness(expert.Forward(negative));
   ```

5. **Update Weights**: If positive > negative
   ```cpp
   int32_t delta = pos_goodness - neg_goodness;
   if (delta > 0) {
       // Reinforce weights
       expert.UpdateWeightsHebbian(positive, delta);
   }
   ```

### Convergence Criteria

Training converges when:
- Goodness delta stabilizes > 0.5
- Load balance score remains < 0.5
- No improvement for `early_stopping_patience` epochs

```cpp
bool HasConverged(const MoETrainingMetrics& metrics) {
    return metrics.avg_goodness_delta < min_threshold ||
           epochs_without_improvement >= early_stopping_patience;
}
```

## Load Balancing During Training

### Why Load Balancing Matters

Without load balancing:
- Router sends all inputs to same few experts
- Other experts never train
- Model collapses to single-expert behavior

### Load Balancing Formula

```
Loss = Forward-Forward Loss + α × Load Balance Loss

where:
  Load Balance Loss = variance(expert_utilization)
  α = load_balance_alpha (typically 0.01)
```

### Adjusting Load Balance

**Too aggressive (α too high):**
- Experts forced to be used equally
- No specialization occurs
- Model quality degrades

**Solution:** Reduce α
```cpp
config.load_balance_alpha = 0.001f;  // Less aggressive
```

**Too weak (α too low):**
- All inputs go to same 2-3 experts
- Other 240+ experts unused
- Wasted capacity

**Solution:** Increase α
```cpp
config.load_balance_alpha = 0.1f;  // More aggressive
```

### Monitoring Expert Utilization

```cpp
auto stats = router.GetLoadStats();

// Print top 10 most used experts
std::vector<std::pair<float, size_t>> util;
for (size_t i = 0; i < stats.utilization_rates.size(); ++i) {
    util.push_back({stats.utilization_rates[i], i});
}

std::sort(util.begin(), util.end(), 
          [](auto& a, auto& b) { return a.first > b.first; });

std::cout << "Top 10 experts:" << std::endl;
for (size_t i = 0; i < 10; ++i) {
    std::cout << "  Expert " << util[i].second 
              << ": " << (util[i].first * 100) << "%" << std::endl;
}
```

## Advanced Training Techniques

### Curriculum Learning

Start with easy examples, gradually increase difficulty:

```cpp
// Sort by complexity (e.g., sequence length)
std::sort(data.begin(), data.end(), 
    [](const auto& a, const auto& b) {
        return a.size() < b.size();
    });

// Train in phases
for (size_t phase = 0; phase < 3; ++phase) {
    size_t end_idx = data.size() * (phase + 1) / 3;
    auto phase_data = std::vector(data.begin(), data.begin() + end_idx);
    
    trainer.Train(phase_data, 20);
}
```

### Expert Dropout

Randomly disable experts during training to improve robustness:

```cpp
// With 10% probability, skip training this expert
std::uniform_real_distribution<float> dist(0.0, 1.0);

for (size_t expert_id : routing.selected_experts) {
    if (dist(rng) > 0.1) {  // 90% chance to train
        auto expert = trainer.GetExpert(expert_id);
        expert->TrainForwardForward(positive, negative);
    }
}
```

### Progressive Expert Activation

Start with few experts, gradually add more:

```cpp
// Start with 8 active experts
config.active_experts = 8;
trainer.Train(data, 20);

// Increase to 16
config.active_experts = 16;
trainer.Train(data, 20);

// Final: 32
config.active_experts = 32;
trainer.Train(data, 60);
```

### Multi-Task Training

Train on multiple tasks simultaneously:

```cpp
// Task A: Code generation
auto code_data = LoadCodeData();

// Task B: Natural language
auto text_data = LoadTextData();

// Task C: Mathematics
auto math_data = LoadMathData();

// Combine
std::vector<std::vector<ternary::Trit>> all_data;
all_data.insert(all_data.end(), code_data.begin(), code_data.end());
all_data.insert(all_data.end(), text_data.begin(), text_data.end());
all_data.insert(all_data.end(), math_data.begin(), math_data.end());

// Shuffle and train
Shuffle(all_data);
trainer.Train(all_data, 100);
```

## Checkpointing and Recovery

### Saving Checkpoints

```cpp
// Save every 10 epochs
if (epoch % 10 == 0) {
    std::string path = "moe_checkpoint_epoch_" + 
                       std::to_string(epoch) + ".chk";
    trainer.SaveCheckpoint(path);
}
```

### Loading Checkpoints

```cpp
// Resume from checkpoint
trainer.LoadCheckpoint("moe_checkpoint_epoch_50.chk");

// Continue training
trainer.Train(data, remaining_epochs);
```

### Best Model Tracking

```cpp
MoETrainingMetrics best_metrics;
float best_score = 0.0f;

for (size_t epoch = 0; epoch < num_epochs; ++epoch) {
    auto metrics = trainer.TrainEpoch(data);
    
    // Score based on goodness delta and load balance
    float score = metrics.avg_goodness_delta * (1.0f - metrics.load_balance_score);
    
    if (score > best_score) {
        best_score = score;
        best_metrics = metrics;
        
        // Save best model
        trainer.SaveCheckpoint("moe_best.chk");
    }
}
```

## Evaluation

### Testing Trained Model

```cpp
// Load test data
auto test_data = LoadData("test_data.bin");

// Evaluate
size_t correct = 0;
for (const auto& sample : test_data) {
    // Route to experts
    auto routing = router.Route(sample);
    
    // Get expert outputs
    for (size_t expert_id : routing.selected_experts) {
        auto expert = trainer.GetExpert(expert_id);
        auto output = expert->Forward(sample);
        
        // Evaluate output quality
        // (task-specific metric)
    }
}

float accuracy = static_cast<float>(correct) / test_data.size();
std::cout << "Test accuracy: " << (accuracy * 100) << "%" << std::endl;
```

### Quality Metrics

```cpp
// 1. Average goodness on test set
float total_goodness = 0.0f;
for (const auto& sample : test_data) {
    auto routing = router.Route(sample);
    for (size_t expert_id : routing.selected_experts) {
        auto expert = trainer.GetExpert(expert_id);
        total_goodness += expert->ComputeGoodness(expert->Forward(sample));
    }
}
float avg_goodness = total_goodness / test_data.size();

// 2. Load balance on test set
auto stats = router.GetLoadStats();
float test_load_balance = stats.imbalance_score;

// 3. Routing efficiency
float avg_latency = 0.0f;
for (const auto& sample : test_data) {
    auto start = std::chrono::high_resolution_clock::now();
    router.Route(sample);
    auto end = std::chrono::high_resolution_clock::now();
    avg_latency += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}
avg_latency /= test_data.size();
```

## Command-Line Interface

The trainer executable supports these options (from `trainer_main.cpp:586-631`):

### Training Control
```bash
--epochs N                    # Number of epochs (default: from config)
--batch-size N                # Samples per batch (default: 8192)
--continuous                  # Run indefinitely, accumulating data
--learning-rate N             # 0-33=LOW, 34-66=MED, 67-100=HIGH
--checkpoint-every N          # Save checkpoint every N epochs
```

### Model Configuration
```bash
--moe-experts N               # Total expert count (REQUIRED)
--moe-top-k N                 # Active experts per forward pass (default: 16)
--context-window N            # Input dimension (default: 4096)
--entanglement-tokens N       # Hash dimension (default: 256)
```

### Data Sources
```bash
--enable-web-apis true        # Use DataSynthesizer APIs (default: true)
--disable-web-apis            # Disable API fetching (offline mode)
--dataset PATH                # Local dataset file (JSONL format)
--base-model PATH             # Checkpoint to resume from
```

### Features
```bash
--steane-correction true      # Enable quantum error correction
--flash-cim true              # Use Flash-CiM acceleration
--sse-mode                    # Enable SSE optimizations
```

### Examples

```bash
# Epoch-based training with 243 experts
q_mini_wasm_v2_trainer.exe --epochs 100 --moe-experts 243

# Continuous training with web APIs
q_mini_wasm_v2_trainer.exe --continuous --moe-experts 243 --enable-web-apis true

# Offline training from local dataset
q_mini_wasm_v2_trainer.exe --epochs 50 --moe-experts 64 --dataset data.jsonl --disable-web-apis

# Resume from checkpoint
q_mini_wasm_v2_trainer.exe --continuous --moe-experts 243 --base-model checkpoint.chk
```

## Troubleshooting

### Training Won't Start

**Symptoms:** No output, no progress

**Check:**
1. Data loaded correctly?
2. Experts initialized?
3. Configuration valid?

```cpp
assert(!data.empty());
assert(trainer.GetExpert(0) != nullptr);
assert(router.Validate243Config());
```

### Training Stuck at Epoch 0

**Symptoms:** No convergence, delta = 0

**Causes:**
- All inputs identical
- Experts not learning (weights not updating)
- Negative samples too similar to positive

**Solutions:**
```cpp
// Check data diversity
assert(data.size() > 1000);

// Verify weight updates
auto expert = trainer.GetExpert(0);
auto old_weights = expert->GetWeights();
trainer.TrainBatch({sample});
auto new_weights = expert->GetWeights();
assert(old_weights != new_weights);  // Should change
```

### Out of Memory

**Symptoms:** OOM crashes during training

**Solutions:**
1. Reduce batch size:
   ```cpp
   config.training_batch_size = 32;
   ```

2. Reduce active experts:
   ```cpp
   router.GetConfig().active_experts = 8;
   ```

3. Use gradient checkpointing (for deep experts):
   ```cpp
   expert_config.num_layers = 2;  // Instead of 4
   ```

### Poor Model Quality

**Symptoms:** Low accuracy, bad generations

**Check:**
1. Enough training data? (Need >10K samples)
2. Enough epochs? (Try 100+)
3. Load balance appropriate? (Check utilization)
4. Data quality? (Validate no corruption)

## Best Practices

1. **Always validate configuration before training**
   ```cpp
   assert(router.Validate243Config());
   ```

2. **Monitor training from epoch 0**
   ```cpp
   config.verbose = true;
   config.log_interval = 1;
   ```

3. **Save checkpoints frequently**
   ```cpp
   if (epoch % 5 == 0) SaveCheckpoint();
   ```

4. **Use validation set to detect overfitting**
   ```cpp
   auto train_metrics = trainer.TrainEpoch(train_data);
   auto val_metrics = Evaluate(val_data);
   
   if (val_metrics.goodness < best_val_goodness * 0.9) {
       // Overfitting! Stop training.
       break;
   }
   ```

5. **Start with small scale, then expand**
   - Test with 16 experts first
   - Scale to 64, then 128, then 243
   - Validate at each scale

## See Also

- [Expert Network Architecture Guide](../architecture/expert-networks.md)
- [243-Expert Configuration Guide](243-expert-config.md)
- [API Reference: MoETrainer](../api/moe_trainer.md)
- [Forward-Forward Learning](../learning/forward-forward.md)

---

*Version: 1.0*  
*Last Updated: April 2026*
