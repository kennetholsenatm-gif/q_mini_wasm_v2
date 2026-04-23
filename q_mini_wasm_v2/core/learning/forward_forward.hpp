#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"

namespace q_mini_wasm_v2::core::learning {

// Tropical sparse edge - only non-zero connections stored
struct TropicalEdge {
    uint32_t target;       // Target neuron index
    ternary::Trit weight; // {-1, +1} - ZERO means no edge (sparse!)
};

// Sparse adjacency list per neuron - tropical geometry structure
struct TropicalLayer {
    std::vector<std::vector<TropicalEdge>> outgoing;  // [source][edges to targets]
    std::vector<ternary::Trit> biases;
};

struct FFConfig {
    size_t num_layers = 3;
    size_t neurons_per_layer = 128;
    int learning_rate = 1;
    int learning_rate_shift = 3;
    uint32_t sparsity_bps = 500;  // 5% = 500 basis points (0-10000, ultra-sparse tropical)
    bool lazy_init = true;  // Don't init weights until first use (MoE optimization)
};

struct LayerGoodness {
    uint32_t positive_goodness = 0;
    uint32_t negative_goodness = 0;
    int32_t delta = 0;
};

class ForwardForwardLearner {
public:
    explicit ForwardForwardLearner(const FFConfig& config);
    ~ForwardForwardLearner();

    // Training
    LayerGoodness train_layer(
        size_t layer_idx,
        const std::vector<std::vector<ternary::Trit>>& positive_data,
        const std::vector<std::vector<ternary::Trit>>& negative_data
    );
    
    LayerGoodness train_layer_entangled(
        size_t layer_idx,
        const std::vector<std::vector<ternary::Trit>>& positive_data,
        const std::vector<std::vector<ternary::Trit>>& negative_data
    );

    // Negative sample generation
    std::vector<std::vector<ternary::Trit>> generate_negative_samples(
        const std::vector<std::vector<ternary::Trit>>& positive_data
    );

    // Goodness computation
    uint32_t compute_goodness(const std::vector<ternary::Trit>& activations) const;
    uint32_t compute_entangled_goodness(const stabilizer::StabilizerTableau& tableau) const;

    // Forward pass
    std::vector<ternary::Trit> forward(const std::vector<ternary::Trit>& input);
    std::vector<ternary::Trit> forward_layer(size_t layer_idx, const std::vector<ternary::Trit>& input);
    std::vector<ternary::Trit> entangled_forward(stabilizer::StabilizerTableau& tableau, const std::vector<ternary::Trit>& input);

    // Weight management
    const std::vector<std::vector<TropicalEdge>>& get_tropical_weights(size_t layer_idx) const;
    void update_weights_hebbian(size_t layer_idx, const std::vector<ternary::Trit>& activations, int32_t delta);
    void reset_weights();

    // Serialization
    /**
     * @brief Serialize all layer weights and biases to binary format
     * @return Byte vector containing serialized model weights
     * 
     * Format: [num_layers][layer1_weights...][layer1_biases...][layer2...]
     * All weights stored as int8_t representing ternary values {-1, 0, 1}
     */
    std::vector<uint8_t> serialize_weights() const;
    
    /**
     * @brief Deserialize and load model weights from binary data
     * @param data Serialized weight data
     * @return true if successful
     */
    bool deserialize_weights(const std::vector<uint8_t>& data);
    
    /**
     * @brief Get number of trainable parameters
     */
    size_t parameter_count() const;

private:
    FFConfig config_;
    // Sparse tropical geometry: only non-zero edges stored
    std::vector<TropicalLayer> tropical_layers_;  // [layer] -> sparse adjacency
    bool weights_initialized_;  // For lazy initialization (MoE memory optimization)

    void initialize_tropical_weights();
    void ensure_weights_initialized();  // Lazy init on first use
    void add_tropical_edge(size_t layer, uint32_t source, uint32_t target, ternary::Trit weight);
    ternary::Trit tropical_activation(int32_t x);
    ternary::Trit compute_weight_update(ternary::Trit pre_synaptic, ternary::Trit post_synaptic);
    ternary::Trit clip_to_ternary(int value);
    
    // Min-plus tropical convolution for forward pass
    std::vector<ternary::Trit> tropical_forward_layer(size_t layer_idx, const std::vector<ternary::Trit>& input);
};

std::unique_ptr<ForwardForwardLearner> create_ff_learner(const FFConfig& config);

} // namespace q_mini_wasm_v2::core::learning
