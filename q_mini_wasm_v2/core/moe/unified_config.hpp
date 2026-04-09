#pragma once

#include "../ternary/trit.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief Unified MoE Configuration for 243-expert scale
 * 
 * Consolidates ExpertConfig, GraphConfig, and EntangledRoutingConfig
 * into a single unified configuration with builder pattern support.
 */
struct UnifiedMoEConfig {
    // Expert topology
    size_t total_experts = 243;        // Total expert count (max 243)
    size_t active_experts = 16;       // Top-K experts to activate
    size_t specialization_dim = 128;   // Expert specialization vector dimension
    
    // Routing configuration
    size_t routing_qutrits = 16;       // Qutrits for routing decisions
    uint32_t routing_dim = 64;         // Routing feature dimension
    
    // Topology configuration
    enum class TopologyType {
        RING,           // Simple ring topology
        SMALL_WORLD,    // Watts-Strogatz small-world (scalable)
        HIERARCHICAL,   // Cluster-based hierarchy (for 243 experts)
        DENSE           // Fully connected (not recommended for >32)
    };
    TopologyType topology = TopologyType::SMALL_WORLD;
    
    // Small-world parameters (fixed-point: 1000 = 1.0)
    int32_t small_world_rewiring_prob_fixed = 300;  // 0.3 probability = 300/1000
    size_t small_world_k = 4;                // Initial neighbors per node
    
    // Hierarchical parameters (for 243 experts)
    size_t cluster_size = 16;          // Experts per cluster
    size_t num_clusters = 16;          // Total clusters (16x16 = 256 capacity)
    
    // Entanglement configuration
    ternary::Trit enable_entanglement = ternary::Trit::POSITIVE;
    uint32_t entanglement_strength = 70;     // 0-100
    uint32_t coherence_threshold = 30;       // Minimum coherence for entanglement
    
    // Load balancing
    ternary::Trit enable_load_balancing = ternary::Trit::POSITIVE;
    uint32_t load_balance_interval = 100;    // Rebalance every N routings
    int32_t load_balance_alpha_fixed = 100;  // 0.1 in fixed-point (100/1000)
    
    // Energy management
    ternary::EnergyTrit energy_budget = ternary::EnergyTrit::MEDIUM;
    ternary::Trit energy_aware_routing = ternary::Trit::POSITIVE;
    
    // Performance tuning
    ternary::Trit use_hierarchical_selection = ternary::Trit::POSITIVE;  // Required for 243 experts
    ternary::Trit use_parallel_topk = ternary::Trit::POSITIVE;           // SYCL-accelerated Top-K
    uint32_t topk_batch_size = 64;           // Batch size for parallel Top-K
    
    // Validation
    ternary::Trit validate() const {
        return (total_experts > 0 && 
               total_experts <= 256 &&
               active_experts > 0 && 
               active_experts <= total_experts &&
               specialization_dim > 0 &&
               routing_qutrits > 0) ? ternary::Trit::POSITIVE : ternary::Trit::ZERO;
    }
    
    // Recommended configs for different scales
    static UnifiedMoEConfig SmallScale() {
        UnifiedMoEConfig config;
        config.total_experts = 16;
        config.active_experts = 4;
        config.topology = TopologyType::RING;
        config.use_hierarchical_selection = ternary::Trit::ZERO;
        return config;
    }
    
    static UnifiedMoEConfig MediumScale() {
        UnifiedMoEConfig config;
        config.total_experts = 64;
        config.active_experts = 8;
        config.topology = TopologyType::SMALL_WORLD;
        config.use_hierarchical_selection = ternary::Trit::POSITIVE;
        return config;
    }
    
    static UnifiedMoEConfig LargeScale() {
        UnifiedMoEConfig config;
        config.total_experts = 243;
        config.active_experts = 16;
        config.topology = TopologyType::HIERARCHICAL;
        config.use_hierarchical_selection = ternary::Trit::POSITIVE;
        config.cluster_size = 16;
        config.num_clusters = 16;
        return config;
    }
};

/**
 * @brief Builder for UnifiedMoEConfig
 */
class MoEConfigBuilder {
public:
    MoEConfigBuilder& TotalExperts(size_t n) {
        config_.total_experts = n;
        return *this;
    }
    
    MoEConfigBuilder& ActiveExperts(size_t k) {
        config_.active_experts = k;
        return *this;
    }
    
    MoEConfigBuilder& SpecializationDim(size_t dim) {
        config_.specialization_dim = dim;
        return *this;
    }
    
    MoEConfigBuilder& Topology(UnifiedMoEConfig::TopologyType t) {
        config_.topology = t;
        return *this;
    }
    
    MoEConfigBuilder& SmallWorldParams(int32_t rewiring_prob_fixed, size_t k) {
        config_.small_world_rewiring_prob_fixed = rewiring_prob_fixed;
        config_.small_world_k = k;
        return *this;
    }
    
    MoEConfigBuilder& HierarchicalParams(size_t cluster_size, size_t num_clusters) {
        config_.cluster_size = cluster_size;
        config_.num_clusters = num_clusters;
        return *this;
    }
    
    MoEConfigBuilder& EnableLoadBalancing(ternary::Trit enable) {
        config_.enable_load_balancing = enable;
        return *this;
    }
    
    MoEConfigBuilder& EnergyBudget(ternary::EnergyTrit budget) {
        config_.energy_budget = budget;
        return *this;
    }
    
    UnifiedMoEConfig Build() const {
        if (config_.validate() != ternary::Trit::POSITIVE) {
            throw std::invalid_argument("Invalid MoE configuration");
        }
        return config_;
    }
    
private:
    UnifiedMoEConfig config_;
};

} // namespace q_mini_wasm_v2::core::moe
