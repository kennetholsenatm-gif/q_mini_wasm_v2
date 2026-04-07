#pragma once

#include "graph_moe_router.hpp"
#include "../moe/router.hpp"
#include "../ternary/trit.hpp"
#include <vector>
#include <memory>

namespace q_mini_wasm_v2::core::qgnn {

/**
 * @brief Migration adapter for transitioning from array-based to graph-native MoE
 * 
 * Provides compatibility layer while enabling gradual migration to QGNN
 * Maintains existing API while internally using graph-native structures
 */
class GraphMigrationAdapter {
public:
    /**
     * @brief Configuration for migration
     */
    struct MigrationConfig {
        bool use_graph_routing;        // Enable graph-native routing
        int32_t migration_ratio_fixed; // Fixed-point ratio (1000 = 1.0)
        bool enable_performance_logging; // Log performance comparisons
        size_t migration_batch_size;   // Batch size for gradual migration
    };
    
    /**
     * @brief Performance comparison metrics
     */
    struct PerformanceMetrics {
        uint32_t array_routing_latency_us;
        uint32_t graph_routing_latency_us;
        ternary::EnergyTrit array_energy;
        ternary::EnergyTrit graph_energy;
        ternary::ProbTrit accuracy_difference;
        int32_t speedup_factor_fixed;           // 1000 = 1.0x
        int32_t energy_efficiency_factor_fixed; // 1000 = 1.0x
    };

private:
    std::unique_ptr<moe::MoERouter> legacy_router_;
    std::unique_ptr<GraphMoERouter> graph_router_;
    MigrationConfig config_;
    PerformanceMetrics performance_metrics_;
    uint32_t migration_step_;

public:
    /**
     * @brief Construct migration adapter
     */
    GraphMigrationAdapter(
        const moe::ExpertConfig& legacy_config,
        const GraphMoERouter::GraphConfig& graph_config,
        const MigrationConfig& migration_config
    );
    
    /**
     * @brief Destructor
     */
    ~GraphMigrationAdapter() = default;
    
    // ========================================================================
    // Unified Routing Interface
    // ========================================================================
    
    /**
     * @brief Unified routing interface (migrates to graph over time)
     * 
     * Automatically chooses between array and graph routing based on migration progress
     * 
     * @param input Input features
     * @param top_k Number of experts to select
     * @return Selected expert indices
     */
    std::vector<size_t> route_unified(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );
    
    /**
     * @brief Array-based routing (legacy)
     */
    std::vector<size_t> route_array(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );
    
    /**
     * @brief Graph-based routing (new)
     */
    std::vector<size_t> route_graph(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );
    
    /**
     * @brief Hybrid routing (combines both approaches)
     */
    std::vector<size_t> route_hybrid(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );

    // ========================================================================
    // Migration Control
    // ========================================================================
    
    /**
     * @brief Advance migration progress
     */
    void advance_migration();
    
    /**
     * @brief Force complete migration to graph
     */
    void complete_migration();
    
    /**
     * @brief Get migration progress (0.0-1.0)
     */
    int32_t get_migration_progress_fixed() const;
    
    /**
     * @brief Check if migration is complete
     */
    bool is_migration_complete() const;

    // ========================================================================
    // Performance Monitoring
    // ========================================================================
    
    /**
     * @brief Compare performance between array and graph routing
     */
    PerformanceMetrics benchmark_performance(
        const std::vector<ternary::Trit>& test_input,
        size_t iterations = 100
    );
    
    /**
     * @brief Get current performance metrics
     */
    const PerformanceMetrics& get_performance_metrics() const;
    
    /**
     * @brief Log performance comparison
     */
    void log_performance_comparison();

    // ========================================================================
    // Compatibility Layer
    // ========================================================================
    
    /**
     * @brief Get expert count (compatible with legacy interface)
     */
    size_t get_expert_count() const;
    
    /**
     * @brief Get active expert count
     */
    size_t get_active_expert_count() const;
    
    /**
     * @brief Update load metrics (both systems)
     */
    void update_load_metrics();
    
    /**
     * @brief Get energy consumption (combined)
     */
    ternary::EnergyTrit get_total_energy() const;

private:
    // ========================================================================
    // Internal Helper Methods
    // ========================================================================
    
    /**
     * @brief Convert graph routing result to legacy format
     */
    std::vector<size_t> convert_graph_result_to_legacy(
        const GraphMoERouter::RoutingResult& graph_result
    ) const;
    
    /**
     * @brief Validate routing consistency between systems
     */
    bool validate_routing_consistency(
        const std::vector<size_t>& array_result,
        const std::vector<size_t>& graph_result
    ) const;
    
    /**
     * @brief Update performance metrics
     */
    void update_performance_metrics(
        uint32_t array_latency,
        uint32_t graph_latency,
        ternary::EnergyTrit array_energy,
        ternary::EnergyTrit graph_energy
    );
    
    /**
     * @brief Should use graph routing for this operation
     */
    bool should_use_graph_routing() const;
};

/**
 * @brief Factory function for creating migration adapter
 */
std::unique_ptr<GraphMigrationAdapter> create_migration_adapter(
    const moe::ExpertConfig& legacy_config,
    const GraphMoERouter::GraphConfig& graph_config,
    const GraphMigrationAdapter::MigrationConfig& migration_config
);

} // namespace q_mini_wasm_v2::core::qgnn
