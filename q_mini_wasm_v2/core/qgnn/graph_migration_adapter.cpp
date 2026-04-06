#include "graph_migration_adapter.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace q_mini_wasm_v2::core::qgnn {

GraphMigrationAdapter::GraphMigrationAdapter(
    const moe::ExpertConfig& legacy_config,
    const GraphMoERouter::GraphConfig& graph_config,
    const MigrationConfig& migration_config
)
    : legacy_router_(moe::create_moe_router(legacy_config))
    , graph_router_(create_graph_moe_router(graph_config))
    , config_(migration_config)
    , migration_step_(0)
{
    // Initialize performance metrics
    performance_metrics_ = PerformanceMetrics{
        .array_routing_latency_us = 0,
        .graph_routing_latency_us = 0,
        .array_energy = ternary::EnergyTrit::MEDIUM,
        .graph_energy = ternary::EnergyTrit::MEDIUM,
        .accuracy_difference = ternary::ProbTrit::MED_PROB,
        .speedup_factor = 1.0,
        .energy_efficiency_factor = 1.0
    };
}

std::vector<size_t> GraphMigrationAdapter::route_unified(
    const std::vector<ternary::Trit>& input,
    size_t top_k
) {
    if (should_use_graph_routing()) {
        return route_graph(input, top_k);
    } else {
        return route_array(input, top_k);
    }
}

std::vector<size_t> GraphMigrationAdapter::route_array(
    const std::vector<ternary::Trit>& input,
    size_t top_k
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Convert input to legacy format (simplified)
    std::vector<std::vector<ternary::Trit>> input_matrix = {input};
    
    // Use legacy router
    auto result = legacy_router_->route_topk(input_matrix, top_k);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Update performance metrics
    performance_metrics_.array_routing_latency_us = static_cast<uint32_t>(duration.count());
    performance_metrics_.array_energy = legacy_router_->get_total_energy_pj() > 0.7 ? 
        ternary::EnergyTrit::HIGH : 
        (legacy_router_->get_total_energy_pj() > 0.3 ? ternary::EnergyTrit::MEDIUM : ternary::EnergyTrit::LOW);
    
    return result;
}

std::vector<size_t> GraphMigrationAdapter::route_graph(
    const std::vector<ternary::Trit>& input,
    size_t top_k
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Use graph router
    auto graph_result = graph_router_->route_quantum_graph(input, top_k);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Convert to legacy format
    auto result = convert_graph_result_to_legacy(graph_result);
    
    // Update performance metrics
    performance_metrics_.graph_routing_latency_us = static_cast<uint32_t>(duration.count());
    performance_metrics_.graph_energy = graph_result.total_energy;
    
    // Calculate speedup and efficiency factors
    if (performance_metrics_.array_routing_latency_us > 0) {
        performance_metrics_.speedup_factor = 
            static_cast<double>(performance_metrics_.array_routing_latency_us) / 
            performance_metrics_.graph_routing_latency_us;
    }
    
    return result;
}

std::vector<size_t> GraphMigrationAdapter::route_hybrid(
    const std::vector<ternary::Trit>& input,
    size_t top_k
) {
    // Get results from both systems
    auto array_result = route_array(input, top_k);
    auto graph_result = route_graph(input, top_k);
    
    // Validate consistency
    bool consistent = validate_routing_consistency(array_result, graph_result);
    
    if (consistent) {
        // Use graph result (more efficient)
        return graph_result;
    } else {
        // Fall back to array result for safety
        if (config_.enable_performance_logging) {
            std::cout << "⚠️ Routing inconsistency detected, using array routing\n";
        }
        return array_result;
    }
}

void GraphMigrationAdapter::advance_migration() {
    if (is_migration_complete()) {
        return;
    }
    
    migration_step_ += config_.migration_batch_size;
    
    // Gradually increase graph routing usage
    double progress = get_migration_progress();
    
    if (progress >= 1.0) {
        complete_migration();
    } else if (config_.enable_performance_logging) {
        std::cout << "📈 Migration progress: " << (progress * 100) << "%\n";
        log_performance_comparison();
    }
}

void GraphMigrationAdapter::complete_migration() {
    config_.use_graph_routing = true;
    config_.migration_ratio = 1.0;
    
    if (config_.enable_performance_logging) {
        std::cout << "✅ Migration to graph-native routing complete!\n";
        log_performance_comparison();
    }
}

double GraphMigrationAdapter::get_migration_progress() const {
    return std::min(1.0, static_cast<double>(migration_step_) / 100.0);
}

bool GraphMigrationAdapter::is_migration_complete() const {
    return config_.migration_ratio >= 1.0 || config_.use_graph_routing;
}

GraphMigrationAdapter::PerformanceMetrics GraphMigrationAdapter::benchmark_performance(
    const std::vector<ternary::Trit>& test_input,
    size_t iterations
) {
    PerformanceMetrics metrics;
    uint32_t total_array_latency = 0;
    uint32_t total_graph_latency = 0;
    
    for (size_t i = 0; i < iterations; ++i) {
        // Benchmark array routing
        auto start = std::chrono::high_resolution_clock::now();
        auto array_result = route_array(test_input, 4);
        auto end = std::chrono::high_resolution_clock::now();
        total_array_latency += static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
        );
        
        // Benchmark graph routing
        start = std::chrono::high_resolution_clock::now();
        auto graph_result = route_graph(test_input, 4);
        end = std::chrono::high_resolution_clock::now();
        total_graph_latency += static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
        );
        
        // Validate consistency
        if (!validate_routing_consistency(array_result, graph_result)) {
            metrics.accuracy_difference = ternary::ProbTrit::LOW_PROB;
        }
    }
    
    // Calculate averages
    metrics.array_routing_latency_us = total_array_latency / iterations;
    metrics.graph_routing_latency_us = total_graph_latency / iterations;
    metrics.array_energy = performance_metrics_.array_energy;
    metrics.graph_energy = performance_metrics_.graph_energy;
    
    // Calculate speedup and efficiency
    if (metrics.array_routing_latency_us > 0) {
        metrics.speedup_factor = 
            static_cast<double>(metrics.array_routing_latency_us) / metrics.graph_routing_latency_us;
    }
    
    // Energy efficiency (lower is better)
    int8_t array_energy_val = static_cast<int8_t>(metrics.array_energy);
    int8_t graph_energy_val = static_cast<int8_t>(metrics.graph_energy);
    if (array_energy_val != 0) {
        metrics.energy_efficiency_factor = 
            static_cast<double>(graph_energy_val) / array_energy_val;
    }
    
    return metrics;
}

const GraphMigrationAdapter::PerformanceMetrics& GraphMigrationAdapter::get_performance_metrics() const {
    return performance_metrics_;
}

void GraphMigrationAdapter::log_performance_comparison() {
    if (!config_.enable_performance_logging) {
        return;
    }
    
    std::cout << "\n📊 Performance Comparison:\n";
    std::cout << "Array Routing Latency: " << performance_metrics_.array_routing_latency_us << " μs\n";
    std::cout << "Graph Routing Latency: " << performance_metrics_.graph_routing_latency_us << " μs\n";
    std::cout << "Speedup Factor: " << performance_metrics_.speedup_factor << "x\n";
    std::cout << "Array Energy: " << static_cast<int>(performance_metrics_.array_energy) << "\n";
    std::cout << "Graph Energy: " << static_cast<int>(performance_metrics_.graph_energy) << "\n";
    std::cout << "Energy Efficiency: " << performance_metrics_.energy_efficiency_factor << "x\n";
    std::cout << "Accuracy Difference: " << static_cast<int>(performance_metrics_.accuracy_difference) << "\n";
}

size_t GraphMigrationAdapter::get_expert_count() const {
    if (should_use_graph_routing()) {
        auto stats = graph_router_->get_graph_stats();
        return stats.node_count;
    } else {
        return legacy_router_->get_config().total_experts;
    }
}

size_t GraphMigrationAdapter::get_active_expert_count() const {
    if (should_use_graph_routing()) {
        return graph_router_->get_routing_stats().total_routings > 0 ? 4 : 2; // Simplified
    } else {
        return legacy_router_->get_config().active_experts;
    }
}

void GraphMigrationAdapter::update_load_metrics() {
    legacy_router_->update_load_balancing();
    graph_router_->update_load_metrics();
}

ternary::EnergyTrit GraphMigrationAdapter::get_total_energy() const {
    if (should_use_graph_routing()) {
        return graph_router_->get_graph_stats().total_energy;
    } else {
        double legacy_energy = legacy_router_->get_total_energy_pj();
        return ternary::energy_to_trit(legacy_energy);
    }
}

std::vector<size_t> GraphMigrationAdapter::convert_graph_result_to_legacy(
    const GraphMoERouter::RoutingResult& graph_result
) const {
    std::vector<size_t> legacy_result;
    legacy_result.reserve(graph_result.selected_experts.size());
    
    for (const auto& node_id : graph_result.selected_experts) {
        legacy_result.push_back(node_id.id);
    }
    
    return legacy_result;
}

bool GraphMigrationAdapter::validate_routing_consistency(
    const std::vector<size_t>& array_result,
    const std::vector<size_t>& graph_result
) const {
    if (array_result.size() != graph_result.size()) {
        return false;
    }
    
    // Check for overlap (at least 50% common elements)
    size_t common_count = 0;
    for (size_t array_expert : array_result) {
        if (std::find(graph_result.begin(), graph_result.end(), array_expert) != graph_result.end()) {
            common_count++;
        }
    }
    
    return common_count >= array_result.size() / 2;
}

void GraphMigrationAdapter::update_performance_metrics(
    uint32_t array_latency,
    uint32_t graph_latency,
    ternary::EnergyTrit array_energy,
    ternary::EnergyTrit graph_energy
) {
    performance_metrics_.array_routing_latency_us = array_latency;
    performance_metrics_.graph_routing_latency_us = graph_latency;
    performance_metrics_.array_energy = array_energy;
    performance_metrics_.graph_energy = graph_energy;
    
    if (array_latency > 0) {
        performance_metrics_.speedup_factor = static_cast<double>(array_latency) / graph_latency;
    }
}

bool GraphMigrationAdapter::should_use_graph_routing() const {
    if (config_.use_graph_routing) {
        return true;
    }
    
    double migration_progress = get_migration_progress();
    double random_factor = (migration_step_ % 100) / 100.0;
    
    return migration_progress > random_factor;
}

std::unique_ptr<GraphMigrationAdapter> create_migration_adapter(
    const moe::ExpertConfig& legacy_config,
    const GraphMoERouter::GraphConfig& graph_config,
    const GraphMigrationAdapter::MigrationConfig& migration_config
) {
    return std::make_unique<GraphMigrationAdapter>(legacy_config, graph_config, migration_config);
}

} // namespace q_mini_wasm_v2::core::qgnn
