# Graph Migration Guide

## Overview

The Graph Migration System provides a seamless transition from array-based MoE routing to graph-native QGNN architecture. This migration is designed to be gradual, reversible, and performance-monitored to ensure zero disruption to existing systems.

## Migration Architecture

### Migration Adapter Components

```cpp
class GraphMigrationAdapter {
private:
    std::unique_ptr<moe::MoERouter> legacy_router_;      // Array-based system
    std::unique_ptr<GraphMoERouter> graph_router_;        // Graph-based system
    MigrationConfig config_;                               // Migration parameters
    PerformanceMetrics performance_metrics_;              // Performance tracking
};
```

### Configuration Options

```cpp
struct MigrationConfig {
    bool use_graph_routing;        // Force graph routing
    double migration_ratio;        // 0.0-1.0 migration progress
    bool enable_performance_logging; // Detailed logging
    size_t migration_batch_size;   // Batch size for gradual migration
};
```

## Migration Strategies

### 1. Unified Routing Interface

The migration adapter provides a single interface that automatically chooses the appropriate routing method:

```cpp
std::vector<size_t> route_unified(
    const std::vector<ternary::Trit>& input,
    size_t top_k
) {
    if (should_use_graph_routing()) {
        return route_graph(input, top_k);
    } else {
        return route_array(input, top_k);
    }
}
```

### 2. Gradual Migration

Control the transition from arrays to graphs:

```cpp
void advance_migration() {
    if (is_migration_complete()) {
        return;
    }
    
    migration_step_ += config_.migration_batch_size;
    double progress = get_migration_progress();
    
    if (progress >= 1.0) {
        complete_migration();
    }
}
```

### 3. Performance Monitoring

Real-time comparison between array and graph routing:

```cpp
PerformanceMetrics benchmark_performance(
    const std::vector<ternary::Trit>& test_input,
    size_t iterations = 100
) {
    // Benchmark both systems
    for (size_t i = 0; i < iterations; ++i) {
        auto array_result = route_array(test_input, 4);
        auto graph_result = route_graph(test_input, 4);
        
        // Validate consistency
        if (!validate_routing_consistency(array_result, graph_result)) {
            metrics.accuracy_difference = ternary::ProbTrit::LOW_PROB;
        }
    }
    
    return metrics;
}
```

## Migration Steps

### Step 1: Setup Migration Adapter

```cpp
// Configure legacy router
moe::ExpertConfig legacy_config{
    .total_experts = 32,
    .active_experts = 4,
    .routing_qutrits = 8
};

// Configure graph router
GraphMoERouter::GraphConfig graph_config{
    .max_experts = 32,
    .active_experts = 4,
    .specialization_dim = 8,
    .energy_budget = ternary::EnergyTrit::MEDIUM
};

// Configure migration
GraphMigrationAdapter::MigrationConfig migration{
    .use_graph_routing = false,        // Start with arrays
    .migration_ratio = 0.0,            // 0% migrated
    .enable_performance_logging = true,
    .migration_batch_size = 5           // 5% per advance
};

// Create adapter
auto adapter = create_migration_adapter(
    legacy_config, graph_config, migration);
```

### Step 2: Baseline Performance Testing

```cpp
// Establish performance baseline
std::vector<ternary::Trit> test_input(8, ternary::Trit::POSITIVE);
auto baseline_metrics = adapter->benchmark_performance(test_input, 100);

std::cout << "Baseline Performance:\n";
std::cout << "Array Latency: " << baseline_metrics.array_routing_latency_us << " μs\n";
std::cout << "Graph Latency: " << baseline_metrics.graph_routing_latency_us << " μs\n";
std::cout << "Speedup Factor: " << baseline_metrics.speedup_factor << "x\n";
```

### Step 3: Gradual Migration

```cpp
// Advance migration in controlled steps
for (size_t step = 0; step < 20; ++step) {
    adapter->advance_migration();
    
    double progress = adapter->get_migration_progress();
    std::cout << "Migration Progress: " << (progress * 100) << "%\n";
    
    // Validate performance
    auto current_metrics = adapter->benchmark_performance(test_input, 50);
    
    // Check for performance regression
    if (current_metrics.speedup_factor < 1.0) {
        std::cout << "Performance regression detected\n";
        // Consider rollback or adjustment
    }
    
    // Log performance comparison
    adapter->log_performance_comparison();
}
```

### Step 4: Complete Migration

```cpp
// Force complete migration
adapter->complete_migration();

// Verify final performance
auto final_metrics = adapter->benchmark_performance(test_input, 200);
std::cout << "Final Migration Results:\n";
std::cout << "Speedup: " << final_metrics.speedup_factor << "x\n";
std::cout << "Energy Efficiency: " << final_metrics.energy_efficiency_factor << "x\n";
std::cout << "Accuracy: " << static_cast<int>(final_metrics.accuracy_difference) << "\n";
```

## Performance Validation

### Consistency Checking

```cpp
bool validate_routing_consistency(
    const std::vector<size_t>& array_result,
    const std::vector<size_t>& graph_result
) {
    if (array_result.size() != graph_result.size()) {
        return false;
    }
    
    // Check for overlap (at least 50% common elements)
    size_t common_count = 0;
    for (size_t array_expert : array_result) {
        if (std::find(graph_result.begin(), graph_result.end(), array_expert) 
            != graph_result.end()) {
            common_count++;
        }
    }
    
    return common_count >= array_result.size() / 2;
}
```

### Performance Metrics

```cpp
struct PerformanceMetrics {
    uint32_t array_routing_latency_us;    // Array routing time
    uint32_t graph_routing_latency_us;    // Graph routing time
    ternary::EnergyTrit array_energy;     // Array energy consumption
    ternary::EnergyTrit graph_energy;     // Graph energy consumption
    ternary::ProbTrit accuracy_difference; // Result consistency
    double speedup_factor;                // Graph speedup over array
    double energy_efficiency_factor;      // Graph energy efficiency
};
```

## Migration Patterns

### 1. Conservative Migration
```cpp
GraphMigrationAdapter::MigrationConfig conservative{
    .use_graph_routing = false,
    .migration_ratio = 0.0,
    .enable_performance_logging = true,
    .migration_batch_size = 1  // Very gradual
};
```

### 2. Aggressive Migration
```cpp
GraphMigrationAdapter::MigrationConfig aggressive{
    .use_graph_routing = false,
    .migration_ratio = 0.0,
    .enable_performance_logging = false,  // Faster, less monitoring
    .migration_batch_size = 20  // Larger steps
};
```

### 3. Immediate Migration
```cpp
GraphMigrationAdapter::MigrationConfig immediate{
    .use_graph_routing = true,   // Force graph routing immediately
    .migration_ratio = 1.0,
    .enable_performance_logging = true,
    .migration_batch_size = 100
};
```

## Troubleshooting

### Performance Regression

**Symptoms**: Graph routing slower than array routing
**Causes**: 
- Small graph sizes where overhead dominates
- Suboptimal graph topology
- Inefficient edge traversal patterns

**Solutions**:
```cpp
// Optimize graph topology
router->create_default_entanglement();

// Use hybrid routing for small workloads
if (input.size() < 8) {
    return route_array(input, top_k);
} else {
    return route_graph(input, top_k);
}
```

### Consistency Issues

**Symptoms**: Array and graph results diverge significantly
**Causes**:
- Different random seed initialization
- Algorithmic differences between systems
- Floating-point vs ternary precision differences

**Solutions**:
```cpp
// Use deterministic seeds
router->set_deterministic_seed(42);

// Validate with tolerance
bool validate_with_tolerance(
    const std::vector<size_t>& array_result,
    const std::vector<size_t>& graph_result,
    double tolerance = 0.3  // 30% tolerance
) {
    size_t common_count = count_common_elements(array_result, graph_result);
    double overlap = static_cast<double>(common_count) / array_result.size();
    return overlap >= (1.0 - tolerance);
}
```

### Memory Issues

**Symptoms**: Memory usage increases during migration
**Causes**:
- Both systems running simultaneously
- Graph data structure overhead
- Performance monitoring data accumulation

**Solutions**:
```cpp
// Clean up legacy system when possible
if (migration_progress > 0.5) {
    legacy_router_.reset();  // Free array system memory
}

// Limit performance monitoring data
if (performance_history_.size() > 1000) {
    performance_history_.erase(performance_history_.begin());
}
```

## Best Practices

### 1. Monitor Performance Continuously
```cpp
// Set up periodic performance checks
std::thread performance_monitor([&adapter]() {
    while (adapter->is_migrating()) {
        auto metrics = adapter->benchmark_performance(test_input, 10);
        adapter->log_performance_comparison();
        std::this_thread::sleep_for(std::chrono::minutes(5));
    }
});
```

### 2. Validate Before Each Migration Step
```cpp
void safe_advance_migration(GraphMigrationAdapter& adapter) {
    // Pre-migration validation
    auto pre_metrics = adapter->benchmark_performance(test_input, 50);
    
    // Advance migration
    adapter->advance_migration();
    
    // Post-migration validation
    auto post_metrics = adapter->benchmark_performance(test_input, 50);
    
    // Check for regression
    if (post_metrics.speedup_factor < pre_metrics.speedup_factor * 0.9) {
        std::cout << "Performance regression detected, rolling back\n";
        // Implement rollback logic
        adapter->set_migration_ratio(previous_progress);
    }
}
```

### 3. Use Hybrid Routing During Transition
```cpp
std::vector<size_t> adaptive_routing(
    const std::vector<ternary::Trit>& input,
    size_t top_k,
    GraphMigrationAdapter& adapter
) {
    // Use graph routing for larger inputs
    if (input.size() >= 16 && adapter->get_migration_progress() > 0.3) {
        return adapter->route_graph(input, top_k);
    }
    // Use array routing for smaller or critical workloads
    else {
        return adapter->route_array(input, top_k);
    }
}
```

## Completion Checklist

### Pre-Migration
- [ ] Baseline performance measurements completed
- [ ] Migration adapter configured and tested
- [ ] Performance monitoring setup
- [ ] Rollback strategy defined

### During Migration
- [ ] Performance metrics continuously monitored
- [ ] Consistency validation passed
- [ ] No significant performance regression
- [ ] Memory usage within acceptable limits

### Post-Migration
- [ ] Final performance benchmarks completed
- [ ] Legacy system safely decommissioned
- [ ] Documentation updated
- [ ] Team training completed

The Graph Migration System ensures a safe, monitored, and performant transition to QGNN graph-native architecture while maintaining system stability and backward compatibility.
