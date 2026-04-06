#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"

namespace q_mini_wasm_v2::core::learning {

/**
 * @brief Entanglement Entropy Goodness Metric
 * 
 * Based on research: "Enhancing the QMINIWASM Framework: Integrating Qutrit 
 * Clifford Entanglement for Extreme-Edge AI"
 * 
 * In the Forward-Forward algorithm, the traditional "goodness" metric (sum of
 * squared activations) is replaced with entanglement entropy.
 * 
 * For positive data, the network maximizes goodness by driving the subsystem
 * toward a stabilizer state with LOW entanglement entropy (ordered state).
 * 
 * For negative data, the network minimizes goodness by driving toward HIGH
 * entanglement entropy (disordered/chaotic state).
 * 
 * This provides a stark contrast for classification without requiring
 * backpropagation or gradient computation.
 */
class EntropyGoodnessMetric {
public:
    /**
     * @brief Construct entropy goodness metric
     * @param num_qutrits Number of qutrits in the system
     */
    explicit EntropyGoodnessMetric(size_t num_qutrits);
    ~EntropyGoodnessMetric();

    // ========================================================================
    // Goodness Computation
    // ========================================================================
    
    /**
     * @brief Compute goodness from stabilizer tableau
     * 
     * Goodness is inversely related to entanglement entropy:
     * goodness = -S(ρ) where S(ρ) is the von Neumann entropy
     * 
     * For stabilizer states, entropy is computed from the rank of
     * the stabilizer subgroup.
     * 
     * @param tableau Stabilizer tableau representing current state
     * @return Goodness value (higher = more ordered = better for positive data)
     */
    uint32_t compute_goodness(const stabilizer::StabilizerTableau& tableau) const;
    
    /**
     * @brief Compute goodness from activations using tropical inner product
     * 
     * Alternative goodness metric using max-plus algebra:
     * goodness = Σ_i max_j(a_i + w_ij)
     * 
     * @param activations Layer activations
     * @return Goodness value
     */
    uint32_t compute_goodness_tropical(const std::vector<ternary::Trit>& activations) const;
    
    /**
     * @brief Compute goodness delta between positive and negative
     * @param positive_goodness Goodness for real data
     * @param negative_goodness Goodness for corrupted data
     * @return Delta (should be positive for well-trained network)
     */
    static int32_t compute_delta(uint32_t positive_goodness, uint32_t negative_goodness);

    // ========================================================================
    // Entanglement Entropy Computation
    // ========================================================================
    
    /**
     * @brief Compute von Neumann entropy of stabilizer state
     * 
     * For a stabilizer state, the entropy of a subsystem is:
     * S = k * log(d) where k is the number of independent generators
     * that act non-trivially on the subsystem, and d is the dimension (3).
     * 
     * @param tableau Stabilizer tableau
     * @return Entropy value
     */
    uint32_t compute_entropy(const stabilizer::StabilizerTableau& tableau) const;
    
    /**
     * @brief Compute entropy of subsystem
     * @param tableau Stabilizer tableau
     * @param subsystem_indices Indices of qutrits in subsystem
     * @return Subsystem entropy
     */
    uint32_t compute_subsystem_entropy(
        const stabilizer::StabilizerTableau& tableau,
        const std::vector<size_t>& subsystem_indices
    ) const;
    
    /**
     * @brief Compute mutual information between two subsystems
     * @param tableau Stabilizer tableau
     * @param subsystem_a First subsystem indices
     * @param subsystem_b Second subsystem indices
     * @return Mutual information I(A:B) = S(A) + S(B) - S(AB)
     */
    int32_t compute_mutual_information(
        const stabilizer::StabilizerTableau& tableau,
        const std::vector<size_t>& subsystem_a,
        const std::vector<size_t>& subsystem_b
    ) const;

    // ========================================================================
    // Learning Signal
    // ========================================================================
    
    /**
     * @brief Compute learning signal for Forward-Forward update
     * 
     * Learning signal = ∂goodness/∂activations
     * For entanglement entropy, this is approximated via finite differences
     * or derived from the stabilizer structure.
     * 
     * @param tableau Current stabilizer state
     * @param activations Current activations
     * @return Learning signal vector
     */
    std::vector<int32_t> compute_learning_signal(
        const stabilizer::StabilizerTableau& tableau,
        const std::vector<ternary::Trit>& activations
    ) const;
    
    /**
     * @brief Check if network is well-trained
     * 
     * Network is well-trained when:
     * - Positive goodness > negative goodness
     * - Delta exceeds threshold
     * 
     * @param positive_goodness Goodness for positive data
     * @param negative_goodness Goodness for negative data
     * @param threshold Minimum delta threshold
     * @return true if well-trained
     */
    static bool is_well_trained(
        uint32_t positive_goodness,
        uint32_t negative_goodness,
        int32_t threshold = 1
    );

private:
    size_t num_qutrits_;
    
    /**
     * @brief Compute stabilizer subgroup rank
     * @param tableau Stabilizer tableau
     * @param subsystem_indices Subsystem to analyze
     * @return Rank of stabilizer subgroup
     */
    size_t compute_stabilizer_rank(
        const stabilizer::StabilizerTableau& tableau,
        const std::vector<size_t>& subsystem_indices
    ) const;
};

/**
 * @brief Factory function
 */
std::unique_ptr<EntropyGoodnessMetric> create_entropy_metric(size_t num_qutrits);

} // namespace q_mini_wasm_v2::core::learning