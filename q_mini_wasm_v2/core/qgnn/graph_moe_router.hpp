#pragma once

#include "graph_native.hpp"
#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"
#include <vector>
#include <memory>
#include <cstdint>

namespace q_mini_wasm_v2::core::qgnn {

/**
 * @brief Graph-native MoE Router for QGNN integration
 * 
 * Replaces tightly-coupled arrays with scalable graph structures
 * Enables efficient quantum message passing and entanglement operations
 */
class GraphMoERouter {
public:
    /**
     * @brief Configuration for graph-based MoE routing
     */
    struct GraphConfig {
        size_t max_experts;           // Maximum number of experts
        size_t active_experts;         // Number of experts to activate
        size_t specialization_dim;     // Dimension of specialization vectors
        ternary::EnergyTrit energy_budget;  // Energy budget per operation
        uint32_t load_threshold;      // Load threshold for scaling
    };
    
    /**
     * @brief Routing result with graph-native structure
     */
    struct RoutingResult {
        std::vector<NodeID> selected_experts;
        std::vector<ternary::Trit> routing_distribution;
        ternary::EnergyTrit total_energy;
        uint32_t routing_latency_us;
        ternary::ProbTrit confidence_score;
    };

private:
    GraphConfig config_;
    std::unique_ptr<QGNNGraph> expert_graph_;
    std::vector<NodeID> expert_ids_;
    uint32_t routing_round_;
    ternary::EnergyTrit accumulated_energy_;

public:
    /**
     * @brief Construct graph-based MoE router
     */
    explicit GraphMoERouter(const GraphConfig& config);
    
    /**
     * @brief Destructor
     */
    ~GraphMoERouter() = default;
    
    // ========================================================================
    // Graph-Native Routing Operations
    // ========================================================================
    
    /**
     * @brief Route input through expert graph using quantum message passing
     * 
     * Implements scalable routing with O(E) complexity instead of O(N²)
     * 
     * @param input Input features in ternary format
     * @param top_k Number of experts to select
     * @return Routing result with selected experts
     */
    RoutingResult route_quantum_graph(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );
    
    /**
     * @brief Perform quantum message passing for expert selection
     * 
     * Uses stabilizer tableau operations for quantum entanglement
     * 
     * @param input Input features
     * @param iterations Number of message passing iterations
     * @return Updated expert states after message passing
     */
    std::vector<ExpertNode*> quantum_message_passing(
        const std::vector<ternary::Trit>& input,
        size_t iterations = 3
    );
    
    /**
     * @brief Select experts using graph-based attention
     * 
     * Computes attention scores over graph structure
     * 
     * @param input Input features
     * @param top_k Number of experts to select
     * @return Selected expert nodes
     */
    std::vector<ExpertNode*> graph_attention_selection(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );
    
    /**
     * @brief Hierarchical expert selection for large graphs
     * 
     * Implements multi-level selection for scalability
     * 
     * @param input Input features
     * @param top_k Number of experts to select
     * @return Selected expert nodes
     */
    std::vector<ExpertNode*> hierarchical_selection(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );

    // ========================================================================
    // Graph Management Operations
    // ========================================================================
    
    /**
     * @brief Add expert to graph
     */
    NodeID add_expert(const std::vector<ternary::Trit>& specialization = {});
    
    /**
     * @brief Add entanglement connection between experts
     */
    void add_entanglement(const NodeID& expert1, const NodeID& expert2, 
                         ternary::Trit strength = ternary::Trit::POSITIVE);
    
    /**
     * @brief Remove expert from graph
     */
    void remove_expert(const NodeID& expert_id);
    
    /**
     * @brief Update expert specialization
     */
    void update_specialization(const NodeID& expert_id, 
                               const std::vector<ternary::Trit>& new_specialization);

    // ========================================================================
    // Load Balancing and Scaling
    // ========================================================================
    
    /**
     * @brief Graph-based load balancing
     * 
     * Balances load across graph structure considering edge weights
     * 
     * @return Load-balanced routing result
     */
    RoutingResult graph_load_balance();
    
    /**
     * @brief Scale expert count based on graph metrics
     * 
     * Dynamically adjusts active experts based on graph load
     * 
     * @param target_load Target load percentage (0-100)
     * @return Updated configuration
     */
    size_t scale_experts_graph_aware(uint32_t target_load);
    
    /**
     * @brief Energy-aware routing through graph
     * 
     * Minimizes energy consumption while maintaining performance
     * 
     * @param input Input features
     * @param top_k Number of experts to select
     * @return Energy-optimized routing result
     */
    RoutingResult energy_aware_routing(
        const std::vector<ternary::Trit>& input,
        size_t top_k
    );

    // ========================================================================
    // Monitoring and Statistics
    // ========================================================================
    
    /**
     * @brief Get graph statistics
     */
    QGNNGraph::GraphStats get_graph_stats() const;
    
    /**
     * @brief Get routing statistics
     */
    struct RoutingStats {
        uint32_t total_routings;
        uint32_t avg_latency_us;
        ternary::EnergyTrit avg_energy_per_routing;
        ternary::ProbTrit avg_confidence;
        int32_t load_balance_score_fixed; // 1000 = 1.0
    };
    
    RoutingStats get_routing_stats() const;
    
    /**
     * @brief Update expert load metrics
     */
    void update_load_metrics();
    
    /**
     * @brief Reset statistics
     */
    void reset_stats();

private:
    // ========================================================================
    // Internal Helper Methods
    // ========================================================================
    
    /**
     * @brief Compute symplectic attention between input and expert
     */
    ternary::ProbTrit compute_symplectic_attention(
        const std::vector<ternary::Trit>& input,
        const ExpertNode* expert
    ) const;
    
    /**
     * @brief Initialize expert graph with default connections
     */
    void initialize_expert_graph();
    
    /**
     * @brief Create default entanglement pattern
     */
    void create_default_entanglement();
    
    /**
     * @brief Validate routing constraints
     */
    bool validate_routing_constraints(const RoutingResult& result) const;
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    RoutingStats routing_stats_;
    bool initialized_;
};

/**
 * @brief Factory function for creating graph-based MoE router
 */
std::unique_ptr<GraphMoERouter> create_graph_moe_router(
    const GraphMoERouter::GraphConfig& config
);

} // namespace q_mini_wasm_v2::core::qgnn
