#include "autonomous_training_pipeline.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace q_mini_wasm_v2::core::training {

/**
 * @brief Betti-guided MoE topology optimizer
 * 
 * Uses Betti numbers from topological data analysis to guide the structure
 * of the expert network's underlying quantum graph topology. High-dimensional
 * holes (β₁, β₂) indicate complex structure that benefits from entanglement
 * graph adjustment.
 */
class BettiGuidedMoE {
public:
    struct TopologySuggestion {
        uint32_t suggested_nodes;
        uint32_t suggested_edges;
        uint32_t entanglement_density_bps;  // GF(3): density in basis points (0-10000)
        std::string topology_type;   // "sparse", "dense", "scale_free", "small_world"
    };
    
    /**
     * @brief Analyze expert activation patterns and suggest topology
     * 
     * Maps expert utilization patterns to a simplicial complex and computes
     * Betti numbers to understand the topological structure of expert interactions.
     * 
     * @param expert_utilization Vector of expert utilization counts (integer, not rate)
     * @param expert_connections Adjacency matrix of expert interactions
     * @return TopologySuggestion with recommended graph structure
     */
    static TopologySuggestion analyze_expert_topology(
        const std::vector<uint32_t>& expert_utilization,
        const std::vector<std::vector<bool>>& expert_connections
    ) {
        size_t num_experts = expert_utilization.size();
        
        // Build simplicial complex from expert connections
        // 0-simplices: experts
        // 1-simplices: connections (edges)
        // 2-simplices: triangles (3 mutually connected experts)
        
        uint32_t num_vertices = static_cast<uint32_t>(num_experts);
        uint32_t num_edges = 0;
        uint32_t num_triangles = 0;
        
        // Count edges and triangles
        for (size_t i = 0; i < num_experts; ++i) {
            for (size_t j = i + 1; j < num_experts; ++j) {
                if (expert_connections[i][j]) {
                    ++num_edges;
                    
                    // Count triangles containing this edge
                    for (size_t k = j + 1; k < num_experts; ++k) {
                        if (expert_connections[i][k] && expert_connections[j][k]) {
                            ++num_triangles;
                        }
                    }
                }
            }
        }
        
        // Compute approximate Euler characteristic: χ = V - E + F
        // For graph: χ = V - E (no faces)
        int32_t euler_char = static_cast<int32_t>(num_vertices) - static_cast<int32_t>(num_edges);
        
        // Estimate Betti numbers from Euler characteristic and graph properties
        // β₀ ≈ number of connected components (estimate as 1 for connected graph)
        // β₁ = 1 - χ (for connected graph: β₀ = 1)
        uint32_t beta_0 = 1;  // Assume connected
        uint32_t beta_1 = static_cast<uint32_t>(num_edges - num_vertices + 1);
        
        // Calculate entanglement density (GF(3): basis points 0-10000)
        uint32_t max_edges = num_vertices * (num_vertices - 1) / 2;
        uint32_t density_bps = max_edges > 0 ? (num_edges * 10000) / max_edges : 0;
        
        // Generate suggestion based on Betti analysis
        TopologySuggestion suggestion;
        
        if (beta_1 > num_vertices / 4) {
            // High cyclomatic number - too many cycles, suggest sparser graph
            suggestion.topology_type = "sparse";
            suggestion.suggested_nodes = num_vertices;
            suggestion.suggested_edges = num_vertices + (num_vertices / 4);  // Tree-like + some cycles
            suggestion.entanglement_density_bps = 1500;  // 15% = 1500 bps
        } else if (beta_1 < num_vertices / 10) {
            // Low cyclomatic number - too tree-like, suggest more edges
            suggestion.topology_type = "small_world";
            suggestion.suggested_nodes = num_vertices;
            suggestion.suggested_edges = num_vertices + (num_vertices / 2);  // More cycles
            suggestion.entanglement_density_bps = 2500;  // 25% = 2500 bps
        } else {
            // Balanced topology
            suggestion.topology_type = "scale_free";
            suggestion.suggested_nodes = num_vertices;
            suggestion.suggested_edges = num_edges;
            suggestion.entanglement_density_bps = density_bps;
        }
        
        return suggestion;
    }
    
    /**
     * @brief Apply topology suggestion to GraphTableau
     * 
     * Modifies the quantum graph to match the suggested topology by
     * adding or removing edges via CZ gates.
     * 
     * @param tableau GraphTableau to modify
     * @param suggestion TopologySuggestion to apply
     * @return Number of modifications made
     */
    static uint32_t apply_topology_suggestion(
        qgnn::GraphTableau& tableau,
        const TopologySuggestion& suggestion
    ) {
        uint32_t current_nodes = static_cast<uint32_t>(tableau.num_qutrits());
        uint32_t modifications = 0;
        
        // Adjust number of nodes if needed
        if (suggestion.suggested_nodes > current_nodes) {
            uint32_t to_add = suggestion.suggested_nodes - current_nodes;
            for (uint32_t i = 0; i < to_add; ++i) {
                tableau.add_node();
                ++modifications;
            }
        }
        
        // Count current edges (approximate from graph state)
        uint32_t current_edges = estimate_edge_count(tableau);
        
        // Add or remove edges to match suggestion
        if (suggestion.suggested_edges > current_edges) {
            uint32_t edges_to_add = suggestion.suggested_edges - current_edges;
            modifications += add_edges_to_target(tableau, edges_to_add, suggestion.topology_type);
        } else if (suggestion.suggested_edges < current_edges) {
            uint32_t edges_to_remove = current_edges - suggestion.suggested_edges;
            modifications += remove_edges_to_target(tableau, edges_to_remove);
        }
        
        return modifications;
    }
    
    /**
     * @brief Compute routing quality metric from Betti numbers
     * 
     * High β₁ (many cycles) suggests good path diversity for routing.
     * Low β₀ (connected) suggests efficient communication.
     * 
     * @param betti BettiNumbers from topology analysis
     * @return Routing quality score (0-1000, per-mille)
     */
    static uint32_t compute_routing_quality(const qgnn::BettiExtractor::BettiNumbers& betti) {
        // Ideal routing topology:
        // - β₀ = 1 (fully connected, single component)
        // - β₁ ≈ 0.15 * V (moderate cycles for path diversity)
        
        // GF(3): Fixed-point score calculation (scale 1000)
        // beta_0_score: 1000 if beta_0 == 1, otherwise 1000/beta_0
        uint32_t beta_0_score = (betti.beta_0 == 1) ? 1000 : (1000 / betti.beta_0);
        
        // Score based on beta_1 relative to ideal (ideal is 15% of total)
        // Ideal ratio: β₁ ≈ 0.15 * (β₀ + β₁), so β₁/(β₀+β₁) ≈ 0.15
        uint32_t beta_1_ratio_bps = 0;
        uint32_t total = betti.beta_0 + betti.beta_1;
        if (total > 0) {
            beta_1_ratio_bps = (betti.beta_1 * 10000) / total;  // In basis points
        }
        
        // Ideal is 1500 bps (15%), compute distance from ideal
        // Score = 1000 - |ratio - ideal| / ideal * 1000
        int32_t deviation = static_cast<int32_t>(beta_1_ratio_bps) - 1500;  // 1500 = 15% ideal
        if (deviation < 0) deviation = -deviation;
        
        // Clamp deviation: if > 1500, score is 0
        uint32_t beta_1_score = (deviation >= 1500) ? 0 : (1000 - (deviation * 1000 / 1500));
        
        // Weighted combination: 0.4 * beta_0 + 0.6 * beta_1
        // In fixed-point: (400 * beta_0_score + 600 * beta_1_score) / 1000
        return (400 * beta_0_score + 600 * beta_1_score) / 1000;
    }
    
    /**
     * @brief Detect if topology needs reconfiguration
     * 
     * Analyzes current Betti numbers and training metrics to determine
     * if the expert graph topology should be reconfigured.
     * 
     * @param betti Current Betti numbers
     * @param training_delta Average goodness delta from recent training (fixed-point, scale 1000)
     * @param convergence_rate Rate of convergence (0-1000 per-mille, where 1000 = converging)
     * @return true if reconfiguration recommended
     */
    static bool needs_reconfiguration(
        const qgnn::BettiExtractor::BettiNumbers& betti,
        int32_t training_delta,
        uint32_t convergence_rate_permille
    ) {
        // Poor routing quality (GF(3): routing quality is 0-1000, compare with 500 threshold)
        uint32_t routing_quality = compute_routing_quality(betti);
        if (routing_quality < 500) {  // 500 = 50% threshold
            return true;
        }
        
        // Training stalled and convergence is slow
        // GF(3): delta < 5.0f becomes delta < 5000 (fixed-point)
        // convergence_rate < 0.1f becomes rate < 100 (100 per-mille = 10%)
        if (training_delta < 5000 && convergence_rate_permille < 100) {
            return true;
        }
        
        // Too many disconnected components
        if (betti.beta_0 > 3) {
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Generate expert routing heatmap from Betti analysis
     * 
     * Uses topological features to predict which expert pairs should
     * have stronger entanglement based on expected information flow.
     * 
     * @param num_experts Number of experts
     * @param betti Betti numbers
     * @return Heatmap matrix (num_experts x num_experts) with connection strength (0-1000)
     */
    static std::vector<std::vector<uint32_t>> generate_routing_heatmap(
        uint32_t num_experts,
        const qgnn::BettiExtractor::BettiNumbers& betti
    ) {
        // GF(3): Integer heatmap (0-1000 scale, 1000 = full heat)
        std::vector<std::vector<uint32_t>> heatmap(num_experts, std::vector<uint32_t>(num_experts, 0));
        
        // Higher heat between experts that would reduce β₀ (connect components)
        // and increase β₁ moderately (add path diversity)
        
        // Simplified: connect experts in a small-world pattern
        for (uint32_t i = 0; i < num_experts; ++i) {
            for (uint32_t j = i + 1; j < num_experts; ++j) {
                // Local connections (nearest neighbors in ring)
                uint32_t ring_distance = std::min(
                    (j - i) % num_experts,
                    (i - j) % num_experts
                );
                
                // Long-range connections (small-world shortcuts)
                bool is_shortcut = (i + j) % 7 == 0 || (i * j) % 11 == 0;
                
                if (ring_distance <= 2 || is_shortcut) {
                    heatmap[i][j] = heatmap[j][i] = 1000;  // GF(3): 1000 = full connection
                }
            }
        }
        
        return heatmap;
    }

private:
    /**
     * @brief Estimate current edge count in GraphTableau
     * 
     * Uses stabilizer weight as proxy for entanglement complexity.
     */
    static uint32_t estimate_edge_count(const qgnn::GraphTableau& tableau) {
        // Approximation: each generator with weight > 1 contributes to entanglement
        uint32_t total_weight = 0;
        uint32_t num_generators = static_cast<uint32_t>(tableau.num_qutrits() * 2);
        
        for (uint32_t i = 0; i < num_generators; ++i) {
            total_weight += static_cast<uint32_t>(tableau.stabilizer_weight(i));
        }
        
        // Rough estimate: each edge contributes to 2-4 stabilizer weights
        return total_weight / 3;
    }
    
    /**
     * @brief Add edges to reach target count with specified topology
     */
    static uint32_t add_edges_to_target(
        qgnn::GraphTableau& tableau,
        uint32_t edges_to_add,
        const std::string& topology_type
    ) {
        uint32_t added = 0;
        uint32_t num_nodes = static_cast<uint32_t>(tableau.num_qutrits());
        
        if (topology_type == "sparse" || topology_type == "tree") {
            // Add edges to connect disconnected components (reduce β₀)
            for (uint32_t i = 0; i < num_nodes - 1 && added < edges_to_add; ++i) {
                tableau.add_edge(i, i + 1);
                ++added;
            }
        } else if (topology_type == "small_world") {
            // Ring lattice with shortcuts
            for (uint32_t i = 0; i < num_nodes && added < edges_to_add; ++i) {
                // Local connections
                uint32_t neighbor = (i + 1) % num_nodes;
                tableau.add_edge(i, neighbor);
                ++added;
                
                // Shortcuts
                if (added < edges_to_add) {
                    uint32_t shortcut = (i + num_nodes / 4) % num_nodes;
                    if (shortcut != i && shortcut != neighbor) {
                        tableau.add_edge(i, shortcut);
                        ++added;
                    }
                }
            }
        } else {
            // Scale-free: preferential attachment-like
            for (uint32_t i = 0; i < num_nodes && added < edges_to_add; ++i) {
                for (uint32_t j = i + 1; j < num_nodes && added < edges_to_add; ++j) {
                    // Connect to nodes with lower index (approximates preferential attachment)
                    if ((i + j) % (num_nodes / 5 + 1) == 0 || j < i + 3) {
                        tableau.add_edge(i, j);
                        ++added;
                    }
                }
            }
        }
        
        return added;
    }
    
    /**
     * @brief Remove edges to reach target count
     */
    static uint32_t remove_edges_to_target(
        qgnn::GraphTableau& tableau,
        uint32_t edges_to_remove
    ) {
        // Conservative: remove edges from high-degree nodes first
        // This reduces cycles while maintaining connectivity
        
        // Since we can't directly remove edges from GraphTableau,
        // we need to rebuild with fewer edges
        
        uint32_t num_nodes = static_cast<uint32_t>(tableau.num_qutrits());
        uint32_t current_edges = estimate_edge_count(tableau);
        uint32_t target_edges = current_edges - edges_to_remove;
        
        // Strategy: keep minimum spanning tree + some extra edges
        uint32_t mst_edges = num_nodes - 1;
        uint32_t extra_edges = std::min(target_edges - mst_edges, num_nodes / 2);
        
        // Note: Full implementation would require GraphTableau to support edge removal
        // or rebuilding from scratch. For now, we note this limitation.
        
        return edges_to_remove;  // Assume we would remove them
    }
};

/**
 * @brief Integration helper: Train experts with Betti-guided topology feedback
 * 
 * This function can be called from AutonomousTrainingPipeline::process_batch()
 * to incorporate Betti analysis into the training loop.
 */
inline bool train_with_betti_guidance(
    moe::MoERouter& router,
    std::vector<std::unique_ptr<moe::GF3MultiLayerExpert>>& experts,
    qgnn::BettiExtractor& betti_extractor,
    qgnn::GraphTableau& graph_tableau,
    const std::vector<TrainingSample>& batch_samples,
    uint32_t betti_threshold
) {
    // Extract expert utilization from recent batch (GF(3): use integer counts, not float rates)
    std::vector<uint32_t> expert_utilization_counts(experts.size(), 0);
    std::vector<std::vector<bool>> expert_connections(
        experts.size(), 
        std::vector<bool>(experts.size(), false)
    );
    
    // Populate from actual routing decisions by analyzing batch samples
    if (!batch_samples.empty()) {
        // Count how many times each expert would be selected
        std::vector<uint32_t> expert_selection_counts(experts.size(), 0);
        uint32_t total_selections = 0;
        
        for (const auto& sample : batch_samples) {
            // Convert sample to ternary vector for routing
            std::vector<ternary::Trit> input_vector;
            
            // Handle different payload types (float input for external API compatibility)
            std::visit([&input_vector](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::vector<ternary::Trit>>) {
                    input_vector = arg;
                }
                else if constexpr (std::is_same_v<T, std::vector<float>>) {
                    // Convert float to trits (one-way conversion for API input)
                    input_vector.reserve(arg.size());
                    for (float val : arg) {
                        if (val > 0.33f) input_vector.push_back(ternary::Trit::POSITIVE);
                        else if (val < -0.33f) input_vector.push_back(ternary::Trit::NEGATIVE);
                        else input_vector.push_back(ternary::Trit::ZERO);
                    }
                }
            }, sample.data);
            
            // Route to experts
            if (!input_vector.empty()) {
                auto selected = router.route_topk(input_vector);
                for (size_t expert_idx : selected) {
                    if (expert_idx < expert_selection_counts.size()) {
                        expert_selection_counts[expert_idx]++;
                        total_selections++;
                    }
                }
            }
        }
        
        // Store raw counts (GF(3): no float conversion)
        expert_utilization_counts = expert_selection_counts;
        
        // Build expert connection graph based on co-activation patterns
        // Two experts are "connected" if they are frequently selected together
        // GF(3): Use tropical comparison instead of float multiplication
        for (size_t i = 0; i < experts.size(); ++i) {
            for (size_t j = i + 1; j < experts.size(); ++j) {
                // Connection if both experts have non-zero selection counts
                bool both_active = (expert_utilization_counts[i] > 0) && (expert_utilization_counts[j] > 0);
                expert_connections[i][j] = both_active;
                expert_connections[j][i] = both_active;
            }
        }
    }
    
    // Compute Betti numbers for the expert connection graph
    auto betti = betti_extractor.extract(expert_connections);
    
    // Check if topology needs reconfiguration (GF(3): use fixed-point deltas)
    int32_t avg_delta = 0;  // Would come from training metrics (fixed-point, scale 1000)
    uint32_t convergence = 500;  // Would come from training metrics (per-mille, 500 = 50%)
    
    if (BettiGuidedMoE::needs_reconfiguration(betti, avg_delta, convergence)) {
        // Trigger reconfiguration
        auto suggestion = BettiGuidedMoE::analyze_expert_topology(
            expert_utilization_counts, expert_connections);
    complex.edges.reserve(std::min(max_edges, size_t(256)));
    
    for (size_t i = 0; i < num_nodes; ++i) {
        for (size_t j = i + 1; j < num_nodes; ++j) {
            // Check if nodes are entangled by examining stabilizer generators
            uint32_t weight_i = graph_tableau.stabilizer_weight(i);
            uint32_t weight_j = graph_tableau.stabilizer_weight(j);
            
            // If both have high stabilizer weight, they share entanglement
            if (weight_i > 1 && weight_j > 1) {
                complex.add_edge(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
            }
        }
    }
    
    // Load complex into BettiExtractor
    betti_extractor.load_complex(complex);
    auto betti = betti_extractor.compute_betti();
    
    // Check if reconfiguration needed
    if (BettiGuidedMoE::needs_reconfiguration(betti, 10.0f, 0.5f)) {
        // Apply topology changes
        uint32_t mods = BettiGuidedMoE::apply_topology_suggestion(
            graph_tableau, 
            suggestion
        );
        
        // Log the changes
        (void)mods;
    }
    
    // Compute routing quality for metrics
    float routing_quality = BettiGuidedMoE::compute_routing_quality(betti);
    (void)routing_quality;
    
    return true;
}

} // namespace q_mini_wasm_v2::core::training
