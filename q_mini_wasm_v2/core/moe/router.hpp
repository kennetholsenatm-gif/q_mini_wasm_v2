#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <random>
#include "../stabilizer/tableau.hpp"
#include "../ternary/trit.hpp"

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief Expert routing configuration
 */
struct ExpertConfig {
    size_t total_experts;       // Total number of available experts
    size_t active_experts;      // Number of experts to activate (Top-K)
    size_t routing_qutrits;     // Number of qutrits for routing decisions
};

/**
 * @brief Entangled routing configuration
 * 
 * Configuration for stabilizer tableau state-based routing with
 * entangled probability distributions for 20-30% efficiency improvement.
 */
struct EntangledRoutingConfig {
    double entanglement_strength;    // Strength of entanglement coupling (0.0-1.0)
    double coherence_threshold;      // Minimum coherence for entangled routing
    size_t measurement_shots;        // Number of measurement shots for probability estimation
    bool use_adaptive_entanglement;  // Adapt entanglement based on input statistics
    double efficiency_target;        // Target efficiency improvement (0.2-0.3 for 20-30%)
    
    // Default constructor with sensible defaults
    EntangledRoutingConfig()
        : entanglement_strength(0.7)
        , coherence_threshold(0.3)
        , measurement_shots(100)
        , use_adaptive_entanglement(true)
        , efficiency_target(0.25)  // 25% improvement target
    {}
};

/**
 * @brief Mixture-of-Experts Router with Tropical Geometry
 * 
 * Based on research: "Sparsity is Combinatorial Depth: Quantifying MoE 
 * Expressivity via Tropical Geometry"
 * 
 * The router uses the max-plus semiring (tropical algebra) where:
 * - Tropical addition: a ⊕ b = max(a, b)
 * - Tropical multiplication: a ⊗ b = a + b
 * 
 * Top-K routing is isomorphic to the k-th elementary symmetric tropical 
 * polynomial, providing combinatorial depth scaling with C(n,k).
 */
class MoERouter {
public:
    /**
     * @brief Construct MoE router
     * @param config Expert configuration
     */
    explicit MoERouter(const ExpertConfig& config);
    
    /**
     * @brief Construct MoE router with entangled routing support
     * @param config Expert configuration
     * @param entangled_config Entangled routing configuration
     */
    MoERouter(const ExpertConfig& config, const EntangledRoutingConfig& entangled_config);
    
    /**
     * @brief Destructor
     */
    ~MoERouter();

    // ========================================================================
    // Routing Operations
    // ========================================================================
    
    /**
     * @brief Route input to Top-K experts using tropical geometry
     * @param input Input features as ternary values
     * @return Indices of selected experts
     */
    std::vector<size_t> route_topk(const std::vector<ternary::Trit>& input);
    
    /**
     * @brief Compute routing logits using tropical inner product
     * @param input Input features
     * @return Routing logits for each expert
     */
    std::vector<double> compute_routing_logits(const std::vector<ternary::Trit>& input);
    
    /**
     * @brief Apply entanglement-based routing via stabilizer tableau
     * @param tableau Stabilizer tableau for routing state
     * @param input Input features
     * @return Entangled routing probabilities
     */
    std::vector<double> entangled_route(
        stabilizer::StabilizerTableau& tableau,
        const std::vector<ternary::Trit>& input
    );

    // ========================================================================
    // Tropical Geometry Operations
    // ========================================================================
    
    /**
     * @brief Tropical inner product for metric computation
     * @param a First vector
     * @param b Second vector
     * @return Tropical inner product: max_i(a_i + b_i)
     */
    static double tropical_inner_product(
        const std::vector<double>& a,
        const std::vector<double>& b
    );
    
    /**
     * @brief Tropical addition (max operation)
     */
    static double tropical_add(double a, double b) {
        return std::max(a, b);
    }
    
    /**
     * @brief Tropical multiplication (addition)
     */
    static double tropical_multiply(double a, double b) {
        return a + b;
    }
    
    /**
     * @brief Compute hypersimplex capacity
     * Number of distinct linear regions = C(total_experts, active_experts)
     */
    size_t compute_hypersimplex_capacity() const;

    // ========================================================================
    // Load Balancing
    // ========================================================================
    
    /**
     * @brief Compute KL-divergence for load balancing
     * @param expert_counts Usage count for each expert
     * @return KL-divergence from uniform distribution
     */
    double compute_load_balance_loss(const std::vector<size_t>& expert_counts) const;
    
    /**
     * @brief Apply load balancing penalty to routing logits
     * @param logits Original routing logits
     * @param expert_counts Historical usage counts
     * @return Balanced logits
     */
    std::vector<double> apply_load_balancing(
        const std::vector<double>& logits,
        const std::vector<size_t>& expert_counts
    ) const;

    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Get expert configuration
     */
    const ExpertConfig& config() const { return config_; }
    
    /**
     * @brief Update expert weights
     * @param expert_idx Expert index
     * @param weights New weight matrix
     */
    void update_expert_weights(size_t expert_idx, const std::vector<std::vector<ternary::Trit>>& weights);

private:
    ExpertConfig config_;
    EntangledRoutingConfig entangled_config_;
    
    // Expert weight matrices (ternary)
    std::vector<std::vector<std::vector<ternary::Trit>>> expert_weights_;
    
    // Routing weight matrix (ternary)
    std::vector<std::vector<ternary::Trit>> routing_weights_;
    
    // Random number generator for entangled routing
    std::mt19937 rng_;
    
    // Entanglement coupling matrix for correlated routing
    std::vector<std::vector<double>> entanglement_coupling_;
    
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    /**
     * @brief Select Top-K indices from logits
     */
    std::vector<size_t> select_topk(const std::vector<double>& logits, size_t k) const;
    
    /**
     * @brief Compute factorial for combinations
     */
    static size_t factorial(size_t n);
    
    /**
     * @brief Compute binomial coefficient C(n, k)
     */
    static size_t binomial_coefficient(size_t n, size_t k);
    
    /**
     * @brief Initialize entanglement coupling matrix
     */
    void initialize_entanglement_coupling();
    
    /**
     * @brief Compute coherence metric for entangled routing
     */
    double compute_coherence(const std::vector<double>& probabilities) const;
};

/**
 * @brief Factory function for creating MoE router
 */
std::unique_ptr<MoERouter> create_moe_router(const ExpertConfig& config);

} // namespace q_mini_wasm_v2::core::moe