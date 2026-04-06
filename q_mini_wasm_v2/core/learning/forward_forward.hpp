#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include "../stabilizer/tableau.hpp"
#include "../ternary/trit.hpp"
#include "../moe/router.hpp"

namespace q_mini_wasm_v2::core::learning {

/**
 * @brief Forward-Forward Learning Configuration
 */
struct FFConfig {
    size_t num_layers;              // Number of hidden layers
    size_t neurons_per_layer;       // Neurons in each layer
    uint32_t learning_rate_shift;   // Bit-shift for learning rate (avoid float)
    uint32_t positive_threshold;    // Goodness threshold for positive data
    uint32_t negative_threshold;    // Goodness threshold for negative data
};

/**
 * @brief Layer goodness metrics
 */
struct LayerGoodness {
    uint32_t positive_goodness;   // Goodness for positive (real) data
    uint32_t negative_goodness;   // Goodness for negative (corrupted) data
    int32_t delta;                // Difference: positive - negative
};

/**
 * @brief Forward-Forward Learning Algorithm
 * 
 * Based on research: "A Unified QMINIWASM Framework: Bridging Qutrit 
 * Stabilizer Formalisms and Extreme-Edge Ternary AI"
 * 
 * Replaces backpropagation with local layer-wise learning using:
 * - Tropical inner product for goodness computation
 * - Hebbian-style weight updates
 * - Non-Parametric Instance Discrimination (NPID)
 * 
 * Key insight: "Sparsity is Combinatorial Depth" - ternary networks
 * achieve high expressivity through combinatorial routing rather than
 * continuous depth.
 */
class ForwardForwardLearner {
public:
    /**
     * @brief Construct Forward-Forward learner
     * @param config Learning configuration
     */
    explicit ForwardForwardLearner(const FFConfig& config);
    
    /**
     * @brief Destructor
     */
    ~ForwardForwardLearner();

    // ========================================================================
    // Training Operations
    // ========================================================================
    
    /**
     * @brief Train one layer using Forward-Forward algorithm
     * @param layer_idx Layer index to train
     * @param positive_data Real training samples
     * @param negative_data Corrupted/generated negative samples
     * @return Layer goodness metrics
     */
    LayerGoodness train_layer(
        size_t layer_idx,
        const std::vector<std::vector<ternary::Trit>>& positive_data,
        const std::vector<std::vector<ternary::Trit>>& negative_data
    );
    
    /**
     * @brief Generate negative samples via corruption
     * @param positive_data Real samples to corrupt
     * @return Corrupted negative samples
     */
    std::vector<std::vector<ternary::Trit>> generate_negative_samples(
        const std::vector<std::vector<ternary::Trit>>& positive_data
    );
    
    /**
     * @brief Compute goodness using tropical inner product
     * @param activations Layer activations
     * @return Goodness score (higher is better for positive data)
     */
    uint32_t compute_goodness(const std::vector<ternary::Trit>& activations) const;

    /**
     * @brief Compute goodness using Entanglement Entropy
     * @param tableau The state tracking tableau
     * @return Goodness score (higher = lower entropy = better for positive data)
     */
    uint32_t compute_entangled_goodness(const stabilizer::StabilizerTableau& tableau) const;

    /**
     * @brief Train a layer using Entanglement Entropy metric and Clifford circuits
     * @param layer_idx Layer index to train
     * @param positive_data Real training samples
     * @param negative_data Corrupted/generated negative samples
     * @return Layer goodness metrics based on Entanglement Entropy
     */
    LayerGoodness train_layer_entangled(
        size_t layer_idx,
        const std::vector<std::vector<ternary::Trit>>& positive_data,
        const std::vector<std::vector<ternary::Trit>>& negative_data
    );

    // ========================================================================
    // Forward Pass
    // ========================================================================
    
    /**
     * @brief Forward pass through all layers
     * @param input Input features
     * @return Output activations
     */
    std::vector<ternary::Trit> forward(const std::vector<ternary::Trit>& input);
    
    /**
     * @brief Forward pass through single layer
     * @param layer_idx Layer index
     * @param input Input to layer
     * @return Layer output
     */
    std::vector<ternary::Trit> forward_layer(
        size_t layer_idx,
        const std::vector<ternary::Trit>& input
    );
    
    /**
     * @brief Compute activations with entanglement
     * @param tableau Stabilizer tableau for state tracking
     * @param input Input features
     * @return Entangled activations
     */
    std::vector<ternary::Trit> entangled_forward(
        stabilizer::StabilizerTableau& tableau,
        const std::vector<ternary::Trit>& input
    );

    // ========================================================================
    // Weight Management
    // ========================================================================
    
    /**
     * @brief Get layer weights
     * @param layer_idx Layer index
     * @return Weight matrix
     */
    const std::vector<std::vector<ternary::Trit>>& get_weights(size_t layer_idx) const;
    
    /**
     * @brief Update layer weights using Hebbian learning
     * @param layer_idx Layer index
     * @param activations Input activations
     * @param delta Goodness delta for learning signal
     */
    void update_weights_hebbian(
        size_t layer_idx,
        const std::vector<ternary::Trit>& activations,
        int32_t delta
    );
    
    /**
     * @brief Reset all weights
     */
    void reset_weights();

    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Get configuration
     */
    const FFConfig& config() const { return config_; }
    
    /**
     * @brief Update learning rate shift
     */
    void set_learning_rate_shift(uint32_t shift) { config_.learning_rate_shift = shift; }

private:
    FFConfig config_;
    
    // Layer weights (ternary)
    std::vector<std::vector<std::vector<ternary::Trit>>> layer_weights_;
    
    // Layer biases (ternary)
    std::vector<std::vector<ternary::Trit>> layer_biases_;
    
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    /**
     * @brief Initialize weights randomly
     */
    void initialize_weights();
    
    /**
     * @brief Ternary activation function
     * @param x Input value
     * @return Ternary output {-1, 0, +1}
     */
    static ternary::Trit ternary_activation(int32_t x);
    
    /**
     * @brief Compute gradient-free weight update
     * @param pre_synaptic Pre-synaptic activation
     * @param post_synaptic Post-synaptic activation
     * @return Weight update delta
     */
    static ternary::Trit compute_weight_update(
        ternary::Trit pre_synaptic,
        ternary::Trit post_synaptic
    );
    
    /**
     * @brief Clip value to ternary range
     */
    static ternary::Trit clip_to_ternary(int value);
};

/**
 * @brief Factory function for creating Forward-Forward learner
 */
std::unique_ptr<ForwardForwardLearner> create_ff_learner(const FFConfig& config);

} // namespace q_mini_wasm_v2::core::learning