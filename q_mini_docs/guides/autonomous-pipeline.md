# Autonomous Training Pipeline

## Overview

The Autonomous Training Pipeline provides continuous, self-supervised training for the q_mini_wasm_v2 framework. Unlike traditional epoch-based training, this pipeline runs indefinitely, continuously acquiring data from knowledge engines and training experts via Forward-Forward learning.

**Source:** `q_mini_wasm_v2/core/training/autonomous_training_pipeline.hpp` and `autonomous_training_pipeline.cpp`

## Pipeline States

```cpp
enum class PipelineState {
    IDLE,                   // Not initialized
    INITIALIZING,          // Setting up components
    READY,                 // Ready to start
    ACQUIRING_DATA,       // DataSynthesizer active
    TRAINING,             // Forward-Forward training
    EVALUATING_TOPOLOGY,  // Betti number computation
    OPTIMIZING_GRAPH,     // Graph topology update
    CHECKPOINTING,        // Saving state
    PAUSED,               // Training paused
    STOPPING,             // Graceful shutdown
    COMPLETE,              // Training finished
    ERROR                  // Error state
};
```

## Key Features

### Continuous Training Mode

```cpp
// Start training - runs in background thread
pipeline.start_training();

// Training continues until explicitly stopped
// Data acquisition and training happen concurrently
```

### Pause/Resume Capability

```cpp
// Pause without losing state
pipeline.pause_training();
// State: PAUSED

// Resume from where we left off
pipeline.resume_training();
// State: TRAINING
```

### Graceful Shutdown

```cpp
// Stop cleanly - saves checkpoint
pipeline.stop_training();
// State: STOPPING → IDLE
```

## Configuration

### PipelineConfig Structure

```cpp
struct PipelineConfig {
    // Data Synthesizer settings
    size_t acquisition_threads = 4;
    size_t perturbation_threads = 2;
    bool enable_knowledge_engine = true;
    
    // Forward-Forward settings
    size_t ff_num_layers = 3;
    size_t ff_layer_width = 128;
    float ff_learning_rate = 0.001f;
    
    // MoE settings
    size_t moe_num_experts = 243;      // Target: 8192
    size_t moe_top_k = 3;              // Default: 16 for larger scales
    size_t moe_input_dim = 64;
    size_t moe_output_dim = 64;
    size_t moe_hidden_dim = 128;
    
    // Betti/Graph settings
    size_t graph_initial_nodes = 64;
    size_t graph_initial_edges = 112;
    size_t betti_max_qutrits = 243;     // Target: 8192
    uint32_t betti_guidance_threshold = 15;  // β₁ threshold for optimization
    
    // Training loop settings
    size_t batch_size = 32;
    size_t num_epochs = 1000;           // Only used if not continuous
    size_t topology_evaluation_interval = 10;  // batches between Betti analysis
    size_t checkpoint_interval = 100;   // epochs between checkpoints
    
    // Control flags
    bool enable_betti_guidance = true;
    bool enable_checkpoints = true;
    bool enable_wui_streaming = true;
};
```

## Betti-Guided Topology Optimization

### Automatic Topology Adjustment

The pipeline monitors Betti numbers during training and adjusts the expert network topology:

```cpp
// High β₁ (1-cycles) indicates many cycles in the graph
// When β₁ > betti_guidance_threshold (default: 15), topology is simplified

if (config_.enable_betti_guidance && betti.beta_1 > config_.betti_guidance_threshold) {
    adjust_topology_based_on_betti(betti);
}
```

### Metrics Tracked

```cpp
struct PipelineMetrics {
    // Forward-Forward metrics
    uint32_t ff_positive_goodness = 0;
    uint32_t ff_negative_goodness = 0;
    int32_t ff_goodness_delta = 0;
    uint64_t ff_total_train_calls = 0;
    
    // MoE metrics
    float moe_load_balance_score = 0.0f;
    float avg_routing_latency_ms = 0.0f;
    std::vector<float> expert_utilization;
    std::vector<int32_t> expert_deltas;
    
    // Betti numbers (topology analysis)
    uint32_t betti_beta_0 = 0;  // Connected components
    uint32_t betti_beta_1 = 0;  // 1-cycles (loops)
    uint32_t betti_beta_2 = 0;  // 2-voids (cavities)
    int32_t euler_characteristic = 0;
    
    // Graph state
    size_t graph_nodes = 0;
    size_t graph_edges = 0;
    std::string graph_topology = "unknown";
    
    // Data Synthesizer stats
    size_t ds_total_acquired = 0;
    size_t ds_total_perturbed = 0;
    size_t ds_api_failures = 0;
    size_t ds_queue_depth = 0;
    
    // Pipeline state
    uint64_t current_epoch = 0;
    uint64_t current_batch = 0;
    float training_progress = 0.0f;  // 0.0 to 100.0
    bool is_running = false;
    std::string status_message;
};
```

## Usage Examples

### Basic Continuous Training

```cpp
#include "core/training/autonomous_training_pipeline.hpp"

// Configure for continuous training
PipelineConfig config;
config.moe_num_experts = 243;           // Current tested scale
config.enable_knowledge_engine = true;  // Use DataSynthesizer
config.enable_betti_guidance = true;    // Auto-optimize topology
config.batch_size = 32;

// Initialize and start
AutonomousTrainingPipeline pipeline;
pipeline.initialize(config);
pipeline.start_training();

// Training runs in background - check status
while (pipeline.get_state() == PipelineState::TRAINING) {
    auto metrics = pipeline.get_metrics();
    std::cout << "Epoch: " << metrics.current_epoch 
              << ", Goodness delta: " << metrics.ff_goodness_delta
              << ", β₁: " << metrics.betti_beta_1 << "\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
}

// Save final model
pipeline.export_model("trained_model.bin");
pipeline.stop_training();
```

### With Pause/Resume

```cpp
// Start training
pipeline.start_training();

// ... after some time ...

// Pause (e.g., for maintenance)
pipeline.pause_training();
assert(pipeline.get_state() == PipelineState::PAUSED);

// Save checkpoint
pipeline.export_model("checkpoint_paused.bin");

// ... later ...

// Resume from pause
pipeline.resume_training();
assert(pipeline.get_state() == PipelineState::TRAINING);
```

### Monitoring with Callbacks

```cpp
// Register state change callback
pipeline.on_state_change([](PipelineState old_state, PipelineState new_state) {
    std::cout << "State: " << static_cast<int>(old_state) 
              << " → " << static_cast<int>(new_state) << "\n";
});

// Register metrics update callback
pipeline.on_metrics_update([](const PipelineMetrics& metrics) {
    // Send to monitoring dashboard, log file, etc.
    log_metrics(metrics);
});

pipeline.start_training();
```

### Manual Topology Evaluation

```cpp
// Force immediate Betti computation
pipeline.trigger_topology_evaluation();

// Get updated metrics
auto metrics = pipeline.get_metrics();
std::cout << "β₀: " << metrics.betti_beta_0 
          << ", β₁: " << metrics.betti_beta_1
          << ", β₂: " << metrics.betti_beta_2 << "\n";

// Manually apply Betti guidance
pipeline.apply_betti_guidance();
```

## Training Loop Internals

### Batch Processing

```cpp
void training_loop() {
    while (!stop_requested_) {
        // Check for pause
        if (pause_requested_) {
            set_state(PipelineState::PAUSED);
            while (pause_requested_ && !stop_requested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (stop_requested_) break;
            set_state(PipelineState::TRAINING);
        }
        
        // Process batch
        process_batch();
        
        // Topology evaluation (every N batches)
        if (current_batch_ % config_.topology_evaluation_interval == 0) {
            evaluate_topology();
        }
        
        // Checkpoint (every N epochs)
        if (current_batch_ % config_.checkpoint_interval == 0) {
            checkpoint_if_needed();
        }
        
        // Update and emit metrics
        update_metrics();
        if (config_.enable_wui_streaming) {
            emit_metrics();
        }
        
        ++current_batch_;
    }
}
```

### Contrastive Sample Generation

For each training sample:

1. **Acquire positive sample** from DataSynthesizer (verified data from APIs)
2. **Generate negative sample** by corrupting ~10% of values
3. **Route to experts** via MoE router (top-K selection)
4. **Train each expert** with Forward-Forward (positive vs negative)
5. **Update metrics** (goodness delta, expert utilization)

## Comparison with Epoch-Based Training

| Feature | Epoch-Based | Autonomous Pipeline |
|---------|-------------|---------------------|
| Duration | Fixed epochs | Indefinite |
| Data | Pre-loaded dataset | Continuous API stream |
| State | Start → Complete | Start → Pause → Resume → Stop |
| Topology | Static | Betti-guided dynamic |
| Best For | Reproducible experiments | Production deployment |

## CLI Integration

The trainer supports both modes via command-line flags:

```bash
# Epoch-based (default)
q_mini_wasm_v2_trainer.exe --epochs 100 --moe-experts 243

# Continuous mode
q_mini_wasm_v2_trainer.exe --continuous --moe-experts 243

# With web APIs
q_mini_wasm_v2_trainer.exe --continuous --enable-web-apis true
```

## Memory Considerations

For large expert counts (target: 8192):

```cpp
// Configure conservatively for available memory
PipelineConfig config;
config.moe_num_experts = 8192;     // Target scale
config.batch_size = 16;            // Smaller batches for memory
config.topology_evaluation_interval = 50;  // Less frequent Betti computation
```

## See Also

- [Data Synthesizer](data-synthesizer.md) - API integration details
- [MoE Training Guide](moe-training.md) - CLI options and training modes
- [Expert Network Architecture](../architecture/expert-networks.md) - Expert topology
- [Betti Numbers](../architecture/stabilizer-tableau.md) - Topology analysis

---

*Version: 2.0*  
*Last Updated: April 2026*  
*Source: `q_mini_wasm_v2/core/training/autonomous_training_pipeline.hpp:1-312`*
