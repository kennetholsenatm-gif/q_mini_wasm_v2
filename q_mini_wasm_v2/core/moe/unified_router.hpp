#pragma once

#include "unified_config.hpp"
#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>

namespace q_mini_wasm_v2::core::moe {

// Forward declarations
class ExpertNetwork;
struct ExpertNode;

/**
 * @brief Unified MoE Router for 243-expert scale
 * 
 * Consolidates legacy MoERouter and GraphMoERouter capabilities:
 * - Tropical geometry routing (from legacy)
 * - Graph-native structures (from graph-native)
 * - Hierarchical selection (new for 243-expert scale)
 * - Sparse entanglement topologies (new)
 */
class UnifiedMoERouter {
public:
    /**
     * @brief Routing result with detailed metrics
     */
    struct RoutingResult {
        std::vector<size_t> selected_experts;
        std::vector<int32_t> routing_weights_fixed;  // Fixed-point: 1000 = 1.0 (was float)
        ternary::EnergyTrit energy_consumed;
        uint32_t latency_us;
        int32_t load_balance_score_fixed;  // Fixed-point: 1000 = 1.0 (was float)
        ternary::Trit used_hierarchical;  // Was bool
    };
    
    /**
     * @brief Load statistics for all experts
     */
    struct LoadStats {
        std::vector<size_t> request_counts;
        std::vector<int32_t> utilization_rates_fixed;  // Fixed-point: 1000 = 1.0 (was float)
        int32_t imbalance_score_fixed;  // Fixed-point: 1000 = 1.0, 0=perfect (was float)
        size_t total_requests;
    };

    explicit UnifiedMoERouter(const UnifiedMoEConfig& config);
    ~UnifiedMoERouter();

    // ========================================================================
    // Core Routing Operations
    // ========================================================================
    
    /**
     * @brief Route input to Top-K experts
     * 
     * Automatically selects routing strategy based on config:
     * - Small scale (<32 experts): Direct tropical routing
     * - Medium scale (32-128): Small-world with parallel Top-K
     * - Large scale (129-243): Hierarchical cluster routing
     */
    RoutingResult Route(const std::vector<ternary::Trit>& input);
    
    /**
     * @brief Batch routing for multiple inputs
     */
    std::vector<RoutingResult> RouteBatch(
        const std::vector<std::vector<ternary::Trit>>& inputs
    );

    // ========================================================================
    // Hierarchical Routing (for 243-expert scale)
    // ========================================================================
    
    /**
     * @brief Two-level hierarchical routing
     * 
     * Level 1: Route to cluster (coarse selection)
     * Level 2: Route to expert within cluster (fine selection)
     */
    RoutingResult HierarchicalRoute(const std::vector<ternary::Trit>& input);
    
    /**
     * @brief Build hierarchical clusters
     */
    void BuildClusters();

    // ========================================================================
    // Tropical Geometry Operations (from legacy router)
    // ========================================================================
    
    /**
     * @brief Compute routing logits using tropical inner product
     */
    std::vector<int32_t> ComputeTropicalLogits(
        const std::vector<ternary::Trit>& input
    );
    
    /**
     * @brief Tropical inner product: max_i(a_i + b_i)
     */
    static int32_t TropicalInnerProduct(
        const std::vector<int32_t>& a,
        const std::vector<int32_t>& b
    );
    
    static int32_t TropicalAdd(int32_t a, int32_t b) { return std::max(a, b); }
    static int32_t TropicalMultiply(int32_t a, int32_t b) { return a + b; }

    // ========================================================================
    // Entanglement Operations
    // ========================================================================
    
    /**
     * @brief Create entanglement topology based on config
     */
    void InitializeEntanglement();
    
    /**
     * @brief Small-world topology (Watts-Strogatz)
     */
    void CreateSmallWorldTopology(int32_t rewiring_prob_fixed, size_t k);  // Fixed-point: 1000 = 1.0 (was double)
    
    /**
     * @brief Hierarchical cluster topology
     */
    void CreateHierarchicalTopology(size_t cluster_size);
    
    /**
     * @brief Ring topology (for small scales)
     */
    void CreateRingTopology();

    // ========================================================================
    // Load Balancing
    // ========================================================================
    
    /**
     * @brief Compute load balancing loss (fixed-point version)
     * @return Loss value in fixed-point (scale 1000 = 1.0)
     */
    int32_t ComputeLoadBalanceLossFixed(const LoadStats& stats);
    
    /**
     * @brief Apply load balancing penalty to logits (fixed-point version)
     * @param logits Fixed-point logits (scale 1000)
     * @param stats Load statistics
     * @return Fixed-point penalized logits
     */
    std::vector<int32_t> ApplyLoadBalancingFixed(
        const std::vector<int32_t>& logits,
        const LoadStats& stats
    );
    
    /**
     * @brief Least-Loaded Expert Parallelism (LLEP) - fixed-point version
     * @param logits Fixed-point logits (scale 1000)
     * @param k Number of experts to select
     * @return Selected expert indices
     */
    std::vector<size_t> LLEPRouteFixed(
        const std::vector<int32_t>& logits,
        size_t k
    );
    
    /**
     * @brief Update load statistics
     */
    void UpdateLoadStats(const std::vector<size_t>& selected_experts);
    
    /**
     * @brief Get current load statistics
     */
    LoadStats GetLoadStats() const;
    
    /**
     * @brief Rebalance expert loads
     */
    void RebalanceLoads();

    // ========================================================================
    // Expert Management
    // ========================================================================
    
    /**
     * @brief Register an expert network
     */
    void RegisterExpert(size_t expert_id, std::shared_ptr<ExpertNetwork> expert);
    
    /**
     * @brief Get expert by ID
     */
    std::shared_ptr<ExpertNetwork> GetExpert(size_t expert_id);
    
    /**
     * @brief Update expert specialization
     */
    void UpdateSpecialization(
        size_t expert_id,
        const std::vector<ternary::Trit>& specialization
    );

    // ========================================================================
    // Configuration and Status
    // ========================================================================
    
    const UnifiedMoEConfig& GetConfig() const { return config_; }
    
    size_t GetTotalExperts() const { return config_.total_experts; }
    size_t GetActiveExperts() const { return config_.active_experts; }
    
    /**
     * @brief Get router statistics
     */
    struct RouterStats {
        uint64_t total_routings;
        uint64_t total_tokens_routed;
        int32_t avg_routing_latency_ms_fixed;  // Fixed-point (was float)
        int32_t avg_load_balance_score_fixed;  // Fixed-point: 1000 = 1.0 (was float)
        ternary::EnergyTrit total_energy_consumed;
    };
    
    RouterStats GetStats() const;
    void ResetStats();

    // ========================================================================
    // 243-Expert Scale Specific
    // ========================================================================
    
    /**
     * @brief Validate configuration for 243-expert scale
     */
    ternary::Trit Validate243Config() const;  // Was bool
    
    /**
     * @brief Estimate memory usage for 243 experts
     */
    size_t EstimateMemoryUsage() const;

    /**
     * @brief Dynamically adjust active expert count based on load
     */
    size_t AdjustExpertScale(uint32_t current_load);

private:
    UnifiedMoEConfig config_;
    
    // Expert storage
    std::vector<std::shared_ptr<ExpertNetwork>> experts_;
    std::vector<std::vector<ternary::Trit>> specializations_;
    
    // Hierarchical structure (for large scale)
    struct Cluster {
        size_t cluster_id;
        std::vector<size_t> expert_ids;
        std::vector<ternary::Trit> centroid;
    };
    std::vector<Cluster> clusters_;
    
    // Load tracking
    LoadStats load_stats_;
    std::vector<uint64_t> expert_request_counts_;
    
    // Entanglement (sparse representation for 243 experts)
    struct EntanglementEdge {
        size_t from;
        size_t to;
        int32_t strength_fixed;  // Fixed-point: 1000 = 1.0 (was float)
    };
    std::vector<EntanglementEdge> entanglement_edges_;
    
    // Statistics
    RouterStats stats_;
    
    // Internal methods
    void InitializeExperts();
    std::vector<size_t> SelectTopK(const std::vector<int32_t>& logits_fixed, size_t k);
    std::vector<size_t> HierarchicalSelect(
        const std::vector<ternary::Trit>& input,
        size_t k
    );
    void RecomputeClusterCentroids();
};

/**
 * @brief Factory function
 */
std::unique_ptr<UnifiedMoERouter> CreateUnifiedRouter(const UnifiedMoEConfig& config);

/**
 * @brief Create 243-expert router with optimal configuration
 */
std::unique_ptr<UnifiedMoERouter> Create243ExpertRouter();

} // namespace q_mini_wasm_v2::core::moe
