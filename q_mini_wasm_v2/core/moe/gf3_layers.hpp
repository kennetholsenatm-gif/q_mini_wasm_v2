#pragma once

#include "expert_network.hpp"
#include <vector>
#include <cstddef>
#include <cstdint>

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief GF(3) Linear Transformation Layer
 * 
 * Implements fully connected layer using GF(3) ternary arithmetic:
 * - Weights: ternary values {-1, 0, 1}
 * - Activations: ternary values {-1, 0, 1}
 * - Operations: tropical (max-plus) algebra
 * 
 * Formula: output[j] = max_i(weight[i][j] + input[i])  (tropical)
 * Or standard: output[j] = sum(input[i] * weight[i][j]) mod 3
 */
class GF3LinearLayer {
public:
    /**
     * @brief Layer configuration
     */
    struct LayerConfig {
        size_t input_dim;
        size_t output_dim;
        bool use_bias = true;
        bool use_tropical = false;  // true = tropical, false = standard GF(3)
    };

    explicit GF3LinearLayer(const LayerConfig& config);
    ~GF3LinearLayer() = default;

    // ========================================================================
    // Core Operations
    // ========================================================================
    
    /**
     * @brief Forward pass using GF(3) arithmetic
     * 
     * Standard GF(3): y[j] = Σ(x[i] × W[i][j]) mod 3
     * Tropical: y[j] = max_i(x[i] + W[i][j])
     * 
     * @param input Ternary input vector
     * @return Ternary output vector
     */
    std::vector<ternary::Trit> Forward(const std::vector<ternary::Trit>& input);
    
    /**
     * @brief Tropical forward pass (max-plus algebra)
     */
    std::vector<ternary::Trit> TropicalForward(const std::vector<ternary::Trit>& input);
    
    /**
     * @brief Standard GF(3) forward pass (modulo 3 arithmetic)
     */
    std::vector<ternary::Trit> StandardForward(const std::vector<ternary::Trit>& input);

    // ========================================================================
    // Weight Management
    // ========================================================================
    
    /**
     * @brief Initialize weights with deterministic ternary values
     */
    void InitializeWeights(int seed = 42);
    
    /**
     * @brief Set weight at specific position
     */
    void SetWeight(size_t in_idx, size_t out_idx, ternary::Trit value);
    
    /**
     * @brief Get weight at specific position
     */
    ternary::Trit GetWeight(size_t in_idx, size_t out_idx) const;
    
    /**
     * @brief Set bias vector
     */
    void SetBias(const std::vector<ternary::Trit>& bias);
    
    /**
     * @brief Get all weights (flattened)
     */
    std::vector<ternary::Trit> GetWeights() const;
    
    /**
     * @brief Get all biases
     */
    std::vector<ternary::Trit> GetBias() const;

    // ========================================================================
    // Training (Forward-Forward)
    // ========================================================================
    
    /**
     * @brief Compute layer goodness for Forward-Forward
     * 
     * Goodness = sum of squared activations
     * For ternary: count of non-zero activations
     */
    uint32_t ComputeGoodness(const std::vector<ternary::Trit>& activations) const;
    
    /**
     * @brief Update weights using Hebbian rule (Forward-Forward)
     * 
     * Δw = learning_rate × (goodness_pos - goodness_neg) × input × output
     * In GF(3): uses ternary multiplication
     * 
     * @param input Input activations
     * @param goodness_delta Delta from positive/negative samples
     * @param learning_rate Learning rate (ternary: typically 1 or -1)
     */
    void UpdateWeightsHebbian(
        const std::vector<ternary::Trit>& input,
        int32_t goodness_delta,
        int8_t learning_rate = 1
    );

    // ========================================================================
    // Information
    // ========================================================================
    
    size_t GetInputDim() const { return config_.input_dim; }
    size_t GetOutputDim() const { return config_.output_dim; }
    size_t GetParameterCount() const { 
        return config_.input_dim * config_.output_dim + 
               (config_.use_bias ? config_.output_dim : 0); 
    }

    /**
     * @brief Get sparsity (fraction of zero weights)
     * @return Q24.8 fixed point representation
     */
    uint32_t GetSparsity() const;

private:
    LayerConfig config_;
    
    // Weights: [input_dim][output_dim]
    std::vector<std::vector<ternary::Trit>> weights_;
    
    // Bias: [output_dim]
    std::vector<ternary::Trit> bias_;
    
    // Helper: GF(3) addition
    static int8_t GF3Add(int8_t a, int8_t b) {
        int8_t sum = a + b;
        if (sum > 1) return -1;  // Wrap: 2 -> -1
        if (sum < -1) return 1;  // Wrap: -2 -> 1
        return sum;
    }
    
    // Helper: GF(3) multiplication
    static int8_t GF3Multiply(int8_t a, int8_t b) {
        if (a == 0 || b == 0) return 0;
        if (a == b) return 1;
        return -1;
    }
    
    // Helper: Tropical addition (max)
    static int8_t TropicalAdd(int8_t a, int8_t b) {
        return std::max(a, b);
    }
    
    // Helper: Tropical multiplication (regular add)
    static int8_t TropicalMultiply(int8_t a, int8_t b) {
        return a + b;
    }
};

/**
 * @brief Multi-layer GF(3) expert network
 * 
 * Composes multiple GF3LinearLayers with activations
 */
class GF3MultiLayerExpert : public ExpertNetwork {
public:
    explicit GF3MultiLayerExpert(const ExpertConfig& config);
    ~GF3MultiLayerExpert() override = default;

    // ExpertNetwork interface implementation
    std::vector<ternary::Trit> Forward(
        const std::vector<ternary::Trit>& input
    ) override;
    
    int32_t TrainForwardForward(
        const std::vector<ternary::Trit>& positive,
        const std::vector<ternary::Trit>& negative
    ) override;

    // Layer management
    void AddLayer(size_t output_dim, bool use_tropical = false);
    void InitializeAllLayers(int seed = 42);
    
    /**
     * @brief Apply ternary activation (identity with threshold)
     * 
     * Values > 0.5 -> 1
     * Values < -0.5 -> -1
     * Otherwise -> 0
     */
    static std::vector<ternary::Trit> TernaryActivation(
        const std::vector<int32_t>& pre_activations
    );

private:
    std::vector<std::unique_ptr<GF3LinearLayer>> layers_;
    bool initialized_ = false;
};

} // namespace q_mini_wasm_v2::core::moe
